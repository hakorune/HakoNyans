        if (!args.baseline_csv.empty()) {
            auto baseline = load_baseline_csv(args.baseline_csv);
            std::cout << "\n=== A/B Diff vs Baseline ===\n";
            std::cout << "Image       dHKN_bytes    dDec(ms)   dSelected   dGain_bytes   dLoss_bytes   d(PNG/HKN)\n";

            std::vector<double> ab_ratios;
            for (const auto& r : rows) {
                auto it = baseline.find(r.image_id);
                if (it == baseline.end()) {
                    std::cout << std::left << std::setw(10) << r.image_name << "(missing in baseline)\n";
                    continue;
                }
                const auto& b = it->second;
                long long d_hkn = (long long)r.hkn_bytes - (long long)b.hkn_bytes;
                double d_dec = r.dec_ms - b.dec_ms;
                long long d_sel = (long long)r.natural_row_selected - (long long)b.natural_row_selected;
                long long d_gain = (long long)r.gain_bytes - (long long)b.gain_bytes;
                long long d_loss = (long long)r.loss_bytes - (long long)b.loss_bytes;
                double d_ratio = r.png_over_hkn - b.png_over_hkn;

                ab_ratios.push_back(d_ratio);

                std::cout << std::left << std::setw(10) << r.image_name
                          << std::right << std::showpos
                          << std::setw(12) << d_hkn
                          << std::setw(12) << std::fixed << std::setprecision(3) << d_dec
                          << std::setw(11) << d_sel
                          << std::setw(13) << d_gain
                          << std::setw(13) << d_loss
                          << std::setw(12) << std::fixed << std::setprecision(4) << d_ratio
                          << std::noshowpos << "\n";
            }

            if (!ab_ratios.empty()) {
                double median_d_ratio = median_value(ab_ratios);
                std::cout << "\nmedian delta(PNG/HKN): "
                          << std::showpos << std::fixed << std::setprecision(4)
                          << median_d_ratio << std::noshowpos << "\n";
            }
        }
