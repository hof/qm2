/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2026 Erik van het Hof and Hermen Reitsma 
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, If not, see <http://www.gnu.org/licenses/>.
 *  
 * File: engine.cpp
 * Implements the chess engine
 */

#include "engine.h"
#include "w17/w17_search.h"
#include "w17/w17_eval.h"
#include <stdexcept>
#include <unordered_map>
#include <tuple>

using namespace std;

namespace engine {

    engine_t _engine;

    void stop() {
        _engine.stop();
    }

    void go() {
        _engine.think();
    }

    void analyse() {
        _engine.analyse();
    }

    void book_calc() {
        _engine.book_calc();
    }

    void new_game(std::string fen) {
        _engine.new_game(fen);
    }

    void set_position(std::string fen, const std::vector<U64> & position_history) {
        _engine.set_position(fen, position_history);
    }

    void set_ponder(bool ponder) {
        _engine.set_ponder(ponder);
    }

    bool is_stopped() {
        return _engine.stop_requested();
    }

    bool is_ponder() {
        return _engine.is_pondering();
    }

    engine_t * instance() {
        return &_engine;
    }

    game_t * settings() {
        return _engine.settings();
    }
}

/**
 * Construct engine thread
 */
engine_t::engine_t() {
    _game.clear();
}

bool engine_t::set_threads(int count) {
    stop();
    try {
        _workers.resize(count);
        return true;
    } catch (const std::exception & error) {
        uci::out(std::string("info string ERROR: could not resize search workers: ") + error.what());
        return false;
    }
}

void engine_t::stop_all() {
    try {
        _workers.wait();
    } catch (const std::exception & error) {
        uci::out(std::string("info string ERROR: search worker failed: ") + error.what());
    } catch (...) {
        uci::out("info string ERROR: search worker failed");
    }
}

bool engine_t::start_search(void * (*function)(void *), bool calculate_book) {
    stop();

    /* Capture the request before any worker starts. The previous played score
     * informs time allocation; every worker receives the same game history.
     * New search state is prepared while all old jobs are idle. */
    {
        std::lock_guard<std::mutex> lock(_result_mutex);
        _game.last_score = _played_scored ? _played_score : 0;
        _game.last_scored = _played_scored;
    }
    try {
        auto session = std::make_shared<search_session_t>();
        session->settings = _game;
        session->root_fen = _root_fen;
        session->position_history = _position_history;
        session->control.ponder.store(session->settings.ponder);
        const int variant = options::get_value("Wild");
        size_t count = calculate_book || variant == 17 ? 1 : options::get_value("Threads");
        if (session->settings.max_nodes) {
            count = std::min<U64>(count, std::max<U64>(1, session->settings.max_nodes / 64));
        }
        std::vector<std::unique_ptr<search_t>> helpers;
        std::unique_ptr<search_t> next_search;
        if (variant == 17) {
            next_search.reset(new w17_search_t(session->root_fen.c_str(), &session->settings,
                &session->control, session->position_history));
        } else {
            next_search.reset(new search_t(session->root_fen.c_str(), &session->settings,
                &session->control, session->position_history));
            for (size_t i = 1; i < count; ++i) {
                helpers.push_back(std::make_unique<search_t>(session->root_fen.c_str(),
                    &session->settings, &session->control, session->position_history));
            }
        }
        if (_workers.size() < count) {
            _workers.resize(count);
        }
        if (variant == 0 && _search && _search->wild == 0) {
            next_search->take_history(*_search);
            for (size_t i = 0; i < std::min(helpers.size(), _helpers.size()); ++i) {
                helpers[i]->take_history(*_helpers[i]);
            }
        }
        if (count > 1) {
            session->team = std::make_unique<search_team_t>(count, session->settings.max_nodes);
            next_search->team = session->team.get();
            for (size_t i = 0; i < helpers.size(); ++i) {
                helpers[i]->team = session->team.get();
                helpers[i]->worker_id = i + 1;
            }
            next_search->start_helpers = [this] { start_helpers(); };
            next_search->finish_helpers = [this] { finish_helpers(); };
        }
        _search = std::move(next_search);
        _helpers = std::move(helpers);
        _session = std::move(session);
    } catch (const std::exception & error) {
        uci::out(std::string("info string ERROR: ") + error.what());
        return false;
    }
    if (calculate_book) {
        _search->game.max_depth = _search->wild == 17
            ? 6 + 2 * popcnt(_search->brd.all(_search->brd.us())) : 20;
    }
    {
        std::lock_guard<std::mutex> lock(_result_mutex);
        _result = search_result_t();
    }
    try {
        _workers.run(0, [this, function, session = _session] {
            function(this);
        });
    } catch (const std::exception & error) {
        uci::out(std::string("info string ERROR: could not dispatch search worker: ") + error.what());
        _session->control.stop_requested.store(true);
        _search.reset();
        return false;
    }
    return true;
}

void engine_t::start_helpers() {
    _search->publish_deadline();
    size_t launched = 0;
    try {
        for (size_t i = 0; i < _helpers.size(); ++i) {
            auto * helper = _helpers[i].get();
            _workers.run(i + 1, [helper, session = _session] {
                try {
                    if (helper->init_root_moves() > 0) {
                        helper->iterative_deepening();
                    }
                } catch (...) {
                    session->team->stopped.store(true, std::memory_order_relaxed);
                    helper->publish_counters();
                    throw;
                }
                helper->publish_counters();
            });
            ++launched;
        }
    } catch (...) {
        _session->team->stopped.store(true, std::memory_order_relaxed);
        for (size_t i = 1; i <= launched; ++i) {
            try {
                _workers.wait_for(i);
            } catch (...) {
                // Preserve the dispatch failure after every launched job is idle.
            }
        }
        throw;
    }
}

/* Completed exact iterations vote together by root move, weighted by depth
 * and score relative to the weakest eligible result. Proven wins take priority;
 * when every result loses decisively, prefer the longest survival. Partial
 * aspiration bounds and unfinished iterations never enter the vote. */
size_t select_search_worker(std::span<search_t * const> workers) {
    int minimum = score::INF;
    for (auto * worker : workers) {
        if (worker->completed_search.depth && worker->completed_search.pv_count) {
            minimum = std::min(minimum, worker->completed_search.score);
        }
    }
    std::unordered_map<int, int64_t> votes;
    for (auto * worker : workers) {
        const auto & result = worker->completed_search;
        if (result.depth && result.pv_count) {
            votes[result.pv[0].to_compact()] += int64_t(result.score - minimum + 9) * result.depth;
        }
    }
    size_t selected = 0;
    bool eligible = false;
    const int decisive = score::TABLEBASE - MAX_PLY;
    for (size_t i = 0; i < workers.size(); ++i) {
        const auto & candidate = workers[i]->completed_search;
        if (!candidate.depth || !candidate.pv_count) {
            continue;
        }
        const auto & best = workers[selected]->completed_search;
        bool choose = !eligible;
        if (eligible) {
            if (candidate.score >= decisive || best.score >= decisive) {
                choose = candidate.score > best.score;
            } else if (candidate.score <= -decisive || best.score <= -decisive) {
                choose = candidate.score > best.score;
            } else {
                choose = std::tuple(votes[candidate.pv[0].to_compact()], candidate.depth, candidate.score)
                    > std::tuple(votes[best.pv[0].to_compact()], best.depth, best.score);
            }
        }
        if (choose) {
            selected = i;
            eligible = true;
        }
    }
    return selected;
}

/* Internal search completion stops helpers without releasing a ponder/infinite
 * result. External protocol control remains separate. Wait for every helper
 * before reading its completed snapshot or replacing any search workspace. */
void engine_t::finish_helpers() {
    _session->team->stopped.store(true, std::memory_order_relaxed);
    for (size_t i = 1; i <= _helpers.size(); ++i) {
        try {
            _workers.wait_for(i);
        } catch (const std::exception & error) {
            uci::out(std::string("info string ERROR: helper search failed: ") + error.what());
        } catch (...) {
            uci::out("info string ERROR: helper search failed");
        }
    }
    std::vector<search_t *> workers{_search.get()};
    for (auto & helper : _helpers) {
        workers.push_back(helper.get());
    }
    const auto * winner = workers[select_search_worker(workers)];
    _search->restore_completed(winner->completed_search);
    _search->attempted_depth = std::max(_search->attempted_depth, winner->attempted_depth);
    _search->interrupted = _search->interrupted || winner->interrupted;
}

/**
 * Initialize the engine for starting a new game
 * @param fen position represented by a FEN string
 */
void engine_t::new_game(std::string fen) {
    stop();
    _search.reset();
    _helpers.clear();
    {
        std::lock_guard<std::mutex> lock(_result_mutex);
        _result = search_result_t();
    }
    {
        std::lock_guard<std::mutex> lock(_result_mutex);
        _played_scored = false;
    }
    trans_table::clear();
    set_position(fen);
}

/**
 * Copies search results
 * @param s search object
 */
void engine_t::copy_results(search_t * s) {
    search_result_t result;
    result.target_found = s->game.target_move.piece
        && s->stack->best_move.equals(&s->game.target_move);
    result.move = s->stack->best_move;
    result.score = s->result_score;
    result.nodes = s->reported_nodes();
    if (s->team) {
        for (size_t i = 0; i < s->team->count; ++i) {
            result.worker_nodes.push_back(s->team->counters[i].nodes.load(std::memory_order_relaxed));
        }
    } else {
        result.worker_nodes.push_back(s->nodes);
    }
    result.score_flag = s->result_flag;
    result.depth = s->result_depth;
    result.completed_depth = s->completed_depth;
    result.attempted_depth = s->attempted_depth;
    result.interrupted = s->interrupted;
    result.source = s->result_source;
    std::lock_guard<std::mutex> lock(_result_mutex);
    _result = result;

    // A ponder search stopped before its ponderhit searched a move the opponent did not play.
    if (!(s->game.ponder && _session->control.ponder.load())) {
        _played_scored = result.move.piece && result.source != result_source_t::book;
        _played_score = result.score;
    }
}

/**
 * Thread function for searching and finding a best move in a chess position
 * @param engineObjPtr pointer to the (parent) engine object
 * @return NULL
 */
void * engine_t::_think(void * engine_p) {

    //initialize
    engine_t * engine = (engine_t*) engine_p;
    search_t * s = engine->_search.get();

    uci::out("info string search_workers " + std::to_string(1 + engine->_helpers.size()));
    s->go();

    // Publish results; the owner releases search state after joining the worker.
    engine->copy_results(s);
    uci::send_bestmove(s->stack->best_move, s->ponder_move);
    return NULL;
}

/**
 * Analyse a chess position by: 
 * - evaluation function
 * - quiescence search
 * - scoring each move using a minimal depth 0 search
 * 
 * This function is mainly useful for testing the above components and the 
 * move ordering by visual inspection of the output.
 */

void engine_t::analyse() {
    stop();
    try {
        if (options::get_value("Wild") == 17) {
            w17_search_t search(_root_fen.c_str());
            uci::out("info string Royal losers evaluation cp " + std::to_string(w17_evaluate(&search)));
            return;
        }
        search_t search(_root_fen.c_str());
        search.stack->in_check = search.brd.in_check();
        const int value = evaluate(&search);
        uci::out(search.stack->in_check ? "info string NNUE static evaluation unavailable in check"
            : "info string NNUE evaluation cp " + std::to_string(value) + " (side to move)");
    } catch (const std::runtime_error & error) {
        uci::out(std::string("info string ERROR: ") + error.what());
    }
}

/**
 * Thread function for searching and finding plausible book moves in a chess position
 * @param engineObjPtr pointer to the (parent) engine object
 * @return NULL
 */
void * engine_t::_book_calc(void * engine_p) {

    //initialize
    engine_t * engine = (engine_t*) engine_p;
    search_t * s = engine->_search.get();

    //get book moves
    s->book_calc();

    // Publish results; the owner releases search state after joining the worker.
    engine->copy_results(s);
    return NULL;
}
