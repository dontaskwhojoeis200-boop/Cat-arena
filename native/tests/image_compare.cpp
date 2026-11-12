// Compares two PNG files of the same size: prints the PSNR and fails below a threshold.
//   cat_arena_image_compare <reference.png> <candidate.png> <min PSNR dB>
#include "cat/scene.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
int main(int argc, char** argv) {
    if(argc != 4) { std::cerr << "usage: cat_arena_image_compare <reference.png> <candidate.png> <min PSNR dB>\n"; return 2; }
    try {
        const auto a = cat::scene::load_image(argv[1]), b = cat::scene::load_image(argv[2]);
        if(a.width != b.width || a.height != b.height) { std::cerr << "FAIL image sizes differ\n"; return 1; }
        double sum = 0;
        std::size_t n = 0;
        for(std::size_t i = 0; i < a.rgba.size(); ++i) { if(i % 4 == 3) continue; const double d = double(a.rgba[i]) - double(b.rgba[i]); sum += d * d; ++n; }
        const double mse = sum / double(n), psnr = mse > 0 ? 10 * std::log10(255.0 * 255.0 / mse) : 99;
        const double minimum = std::atof(argv[3]);
        std::cout << (psnr >= minimum ? "PASS " : "FAIL ") << argv[2] << " vs reference: PSNR " << psnr << " dB (minimum " << minimum << ")\n";
        return psnr >= minimum ? 0 : 1;
    } catch(const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
