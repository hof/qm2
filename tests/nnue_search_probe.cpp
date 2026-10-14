#include "engine.h"
#include "nnue_audit.h"
#include <charconv>
#include <fstream>
#include <iomanip>

namespace uci { bool valid_fen(const std::string & text); }

int main(int argc, char ** argv) {
    try {
        if (argc != 8 && argc != 9) throw std::runtime_error("network positions evaluator mode policy nodes depth [cutoffs] required");
        auto number = [](const char * text) {
            U64 value;
            const std::string input(text);
            const auto result = std::from_chars(input.data(), input.data() + input.size(), value);
            if (result.ec != std::errc() || result.ptr != input.data() + input.size())
                throw std::runtime_error("invalid integer argument");
            return value;
        };
        const U64 policy = number(argv[5]), nodes = number(argv[6]), depth = number(argv[7]);
        if (policy > 63 || depth < 1 || depth > MAX_PLY) throw std::runtime_error("invalid policy/depth");
        const std::string evaluator(argv[3]), mode(argv[4]);
        if ((evaluator != "hce" && evaluator != "nnue")
                || (mode != "off" && mode != "verify" && mode != "full" && mode != "replay" && mode != "fixed" && mode != "quiet"))
            throw std::runtime_error("invalid evaluator/mode");
        if ((mode == "fixed" || mode == "quiet") && (policy || evaluator != "nnue"))
            throw std::runtime_error("matched horizon requires NNUE and policy zero");
        std::ifstream input(argv[2]);
        if (!input) throw std::runtime_error("cannot read positions");
        std::vector<std::pair<std::string, std::string>> positions;
        std::string line;
        while (std::getline(input, line)) {
            const auto split = line.find('\t');
            if (split == std::string::npos || !uci::valid_fen(line.substr(split + 1)))
                throw std::runtime_error("invalid position line");
            positions.emplace_back(line.substr(0, split), line.substr(split + 1));
        }
        if (positions.empty()) throw std::runtime_error("empty positions");
        std::ofstream cutoffs;
        if (argc == 9) {
            cutoffs.open(argv[8]);
            if (!cutoffs) throw std::runtime_error("cannot write cutoffs");
        }
        magic::init();
        uci::silent(true);
        trans_table::set_size(64);
        options::get_option("OwnBook")->value = 0;
        options::get_option("DrawContempt")->value = 0;
        options::get_option("SyzygyProbeLimit")->value = 0;
        options::get_option("UseNNUE")->value = evaluator == "nnue";
        std::string error;
        if (!nnue::set_path(argv[1], error)) throw std::runtime_error(error);
        for (const auto & [id, fen] : positions) {
            trans_table::clear();
            pawn_table::clear();
            material_table::clear();
            auto storage = std::make_unique<search_t>(fen.c_str());
            auto & search = *storage;
            search.nnue_audit.mode = mode == "off" ? nnue::audit_mode_t::off
                : mode == "full" ? nnue::audit_mode_t::full : nnue::audit_mode_t::verify;
            search.beta_pruning = policy & 1;
            search.ffp_enabled = policy & 2;
            search.null_enabled = policy & 4;
            search.lmr_enabled = policy & 8;
            search.nnue_audit.q_delta = policy & 16;
            search.nnue_audit.q_see = policy & 32;
            search.nnue_audit.record_cutoffs = argc == 9;
            search.nnue_audit.fixed_horizon = mode == "fixed";
            search.nnue_audit.aligned_quiescence = mode == "quiet";
            search.game.max_nodes = nodes;
            search.game.max_depth = depth;
            if (mode == "replay") {
                search.stack->in_check = search.brd.in_check();
                search.result_score = search.pvs(-score::INF, score::INF, depth);
                search.completed_depth = search.stop_all ? 0 : depth;
            } else search.go();
            if (search.brd.ply != 0 || search.brd.to_string() != fen)
                throw std::runtime_error("search failed to restore root");
            const auto & counts = search.nnue_audit.counters;
            for (const auto & cutoff : search.nnue_audit.beta_cutoffs)
                cutoffs << "{\"id\":" << std::quoted(id) << ",\"fen\":" << std::quoted(cutoff.fen)
                    << ",\"alpha\":" << cutoff.alpha << ",\"beta\":" << cutoff.beta
                    << ",\"depth\":" << cutoff.depth << ",\"ply\":" << cutoff.ply
                    << ",\"eval\":" << cutoff.eval << "}\n";
            std::cout << "{\"id\":" << std::quoted(id)
                << ",\"evaluator\":" << std::quoted(evaluator) << ",\"mode\":" << std::quoted(mode)
                << ",\"policy\":" << policy << ",\"nodes\":" << search.nodes
                << ",\"depth\":" << search.completed_depth << ",\"score\":" << search.result_score
                << ",\"bestmove\":" << std::quoted(search.stack->best_move.to_string())
                << ",\"pv\":" << std::quoted(search.pv_to_string())
                << ",\"calls\":" << counts.calls << ",\"cached\":" << counts.cached_returns
                << ",\"reuses\":" << counts.reuses << ",\"updates\":" << counts.updates
                << ",\"root_refreshes\":" << counts.root_refreshes
                << ",\"king_refreshes\":" << counts.king_refreshes
                << ",\"parent_refreshes\":" << counts.parent_refreshes
                << ",\"q_delta_prunes\":" << counts.q_delta_prunes
                << ",\"q_see_prunes\":" << counts.q_see_prunes << "}" << std::endl;
        }
    } catch (const std::exception & error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
