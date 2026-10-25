Fathom Syzygy probing library, MIT licensed.

Source: https://github.com/jdart1/Fathom
Revision: c9c6fef0dddc05d2e242c183acf5833149ab676d

Local change: cast the nonnegative population count to unsigned in
`tb_expand_mate` before comparing with unsigned `cardinalityDTM`. This preserves
the comparison and removes GCC's signedness warning; no warnings are suppressed.
Other upstream source/license content is unchanged. Maxima's interface and
policies live in src/tablebase.h and src/tablebase.cpp. Build the decoder as C11;
keep third-party identifiers isolated from project code.
