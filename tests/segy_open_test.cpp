#include "kubik/SegyCube.hpp"

#include <segyio/segy.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

void dumpTraceHeaders(const char* path, int count) {
    segy_file* fp = segy_open(path, "rb");
    if (!fp) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return;
    }
    if (segy_collect_metadata(fp, -1, -1, -1) != SEGY_OK) {
        std::fprintf(stderr, "collect_metadata failed\n");
        segy_close(fp);
        return;
    }

    std::printf("file: %s\n", path);
    std::printf("  traces=%d samples=%d format=%d elemsize=%d\n",
                fp->metadata.tracecount,
                fp->metadata.samplecount,
                fp->metadata.format,
                fp->metadata.elemsize);

    const int fields[] = {9, 17, 21, 25, 73, 77, 181, 185, 189, 193};
    char hdr[SEGY_TRACE_HEADER_SIZE];
    for (int tr = 0; tr < count && tr < fp->metadata.tracecount; ++tr) {
        if (segy_read_standard_traceheader(fp, tr, hdr) != SEGY_OK) {
            continue;
        }
        std::printf("  trace %d:", tr);
        for (int f : fields) {
            int v = 0;
            if (segy_get_tracefield_int(hdr, f, &v) == SEGY_OK) {
                std::printf(" f%d=%d", f, v);
            }
        }
        std::printf("\n");
    }

    const int nt = fp->metadata.samplecount;
    const int es = fp->metadata.elemsize;
    std::vector<char> raw(static_cast<std::size_t>(nt) * static_cast<std::size_t>(es));
    if (segy_readtrace(fp, 0, raw.data()) == SEGY_OK) {
        segy_to_native(fp->metadata.format, nt, raw.data());
        float max_abs = 0.f;
        if (es == 4) {
            const float* s = reinterpret_cast<const float*>(raw.data());
            for (int i = 0; i < nt; ++i) {
                max_abs = std::max(max_abs, std::abs(s[i]));
            }
        }
        std::printf("  trace0 max|amp|=%g\n", static_cast<double>(max_abs));
    }

    segy_close(fp);
}

float sliceMaxAbs(const std::vector<float>& slice) {
    float m = 0.f;
    for (float v : slice) {
        m = std::max(m, std::abs(v));
    }
    return m;
}

int sliceNonZero(const std::vector<float>& slice) {
    int n = 0;
    for (float v : slice) {
        if (v != 0.f) {
            ++n;
        }
    }
    return n;
}

}  // namespace

int main(int argc, char** argv) {
    std::string path = "/home/ssn/workdev/cubetools/data/both_stime_25x5.sgy";
    if (argc > 1) {
        path = argv[1];
    }

    dumpTraceHeaders(path.c_str(), 8);

    kubik::SegyCube cube;
    try {
        cube.load(path);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "SegyCube::load failed: %s\n", ex.what());
        return 1;
    }

    const auto& g = cube.geometry();
    std::printf("geometry: IL %d..%d (%d), XL %d..%d (%d), T=%d dt=%g ms\n",
                g.min_il,
                g.max_il,
                g.n_il,
                g.min_xl,
                g.max_xl,
                g.n_xl,
                g.n_t,
                static_cast<double>(g.dt_ms));
    std::printf("  sample format (binary hdr): %d = %s\n",
                g.sample_format,
                kubik::segySampleFormatName(g.sample_format));

    float vmin99 = 0.f;
    float vmax99 = 0.f;
    cube.clipRange(99.f, vmin99, vmax99);
    std::printf("  clip 99%%: vmin=%g vmax=%g (percentiles [1, 99])\n",
                static_cast<double>(vmin99),
                static_cast<double>(vmax99));
    float vmin95 = 0.f;
    float vmax95 = 0.f;
    cube.clipRange(95.f, vmin95, vmax95);
    std::printf("  clip 95%%: vmin=%g vmax=%g (percentiles [5, 95])\n",
                static_cast<double>(vmin95),
                static_cast<double>(vmax95));

    if (g.sample_format != SEGY_IEEE_FLOAT_4_BYTE) {
        std::fprintf(stderr,
                     "WARN: expected IEEE float (5) for test file, got %d\n",
                     g.sample_format);
    }

    const int il = g.n_il / 2;
    const auto inline_slice = cube.readInlineSlice(il);
    std::printf("inline[%d] label=%d: pixels=%zu nonzero=%d max|amp|=%g\n",
                il,
                cube.inlineLabel(il),
                inline_slice.size(),
                sliceNonZero(inline_slice),
                static_cast<double>(sliceMaxAbs(inline_slice)));

    if (sliceMaxAbs(inline_slice) <= 0.f) {
        std::fprintf(stderr, "FAIL: inline slice is empty\n");
        return 2;
    }

    kubik::SegyCube cube_mem;
    try {
        kubik::CubeLoadOptions mem_opts;
        mem_opts.mode = kubik::CubeLoadMode::InMemory;
        cube_mem.load(path, mem_opts);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "FAIL: InMemory load: %s\n", ex.what());
        return 2;
    }
    if (!cube_mem.isInMemory()) {
        std::fprintf(stderr, "FAIL: InMemory mode did not load volume\n");
        return 2;
    }
    const auto mem_il = cube_mem.readInlineSlice(il);
    if (mem_il.size() != inline_slice.size() ||
        sliceMaxAbs(mem_il) != sliceMaxAbs(inline_slice)) {
        std::fprintf(stderr, "FAIL: InMemory inline slice mismatch\n");
        return 2;
    }

    int pos = 0, neg = 0;
    for (float v : inline_slice) {
        if (v > 0.f) {
            ++pos;
        } else if (v < 0.f) {
            ++neg;
        }
    }
    std::printf("  sign: +%d / -%d samples\n", pos, neg);

    const std::string cropped_path = path + ".cropped.sgy";
    const kubik::CropBounds crop{0, g.n_il - 1, 0, 2, 5, g.n_t - 1};
    try {
        cube.saveCropped(cropped_path, crop);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "FAIL: saveCropped: %s\n", ex.what());
        return 3;
    }

    kubik::SegyCube cropped;
    try {
        cropped.load(cropped_path);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "FAIL: reload cropped: %s\n", ex.what());
        return 3;
    }
    const auto& cg = cropped.geometry();
    std::printf("cropped: IL×XL×T = %d×%d×%d format=%d\n", cg.n_il, cg.n_xl, cg.n_t, cg.sample_format);
    if (cg.sample_format != SEGY_IEEE_FLOAT_4_BYTE) {
        std::fprintf(stderr, "FAIL: cropped format is not IEEE (5)\n");
        return 3;
    }
    if (cg.n_xl != 3 || cg.n_t != g.n_t - 5) {
        std::fprintf(stderr, "FAIL: unexpected cropped geometry\n");
        return 3;
    }
    std::remove(cropped_path.c_str());

    std::printf("OK\n");
    return 0;
}
