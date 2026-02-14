static void print_mode_stat_row(const std::string& key, uint64_t value, uint64_t total) {
    double pct = (total > 0) ? (100.0 * (double)value / (double)total) : 0.0;
    std::cout << "  " << std::left << std::setw(20) << key
              << std::right << std::setw(10) << value
              << std::setw(10) << std::fixed << std::setprecision(2) << pct << "%\n";
}

void print_lossless_mode_stats(const GrayscaleEncoder::LosslessModeDebugStats& s) {
#include "bench_bit_accounting_lossless_text_report_overview.inc"
#include "bench_bit_accounting_lossless_text_report_filter_lo_screen.inc"
#include "bench_bit_accounting_lossless_text_report_route_profile.inc"
}
