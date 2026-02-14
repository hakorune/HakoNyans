public:
    static std::vector<FileHeader::BlockType> decode_block_types(
        const uint8_t* val, size_t sz, int nb,
        uint16_t file_version = FileHeader::MIN_SUPPORTED_VERSION
    ) {
        return lossless_block_types_codec::decode_block_types(
            val,
            sz,
            nb,
            file_version,
            [](const uint8_t* data, size_t size, size_t raw_count) {
                return GrayscaleDecoder::decode_byte_stream(data, size, raw_count);
            },
            [](const uint8_t* data, size_t size, size_t raw_count) {
                return TileLZ::decompress(data, size, raw_count);
            }
        );
    }

    static std::vector<Token> decode_stream(const uint8_t* s, size_t sz) {
        if (sz < 8) return {};
        uint32_t cs; std::memcpy(&cs, s, 4);
        std::vector<uint32_t> f(cs/4); std::memcpy(f.data(), s+4, cs);
        CDFTable cdf = CDFBuilder().build_from_freq(f);
        uint32_t tc; std::memcpy(&tc, s+4+cs, 4);
        uint32_t rs; std::memcpy(&rs, s+8+cs, 4);
        FlatInterleavedDecoder dec(std::span<const uint8_t>(s+12+cs, rs));
        std::vector<Token> t; t.reserve(tc);
        for (uint32_t i=0; i<tc; i++) t.emplace_back((TokenType)dec.decode_symbol(cdf), 0, 0);
        uint32_t rc; size_t off = 12+cs+rs; std::memcpy(&rc, s+off, 4); off += 4;
        size_t ri = 0; for (auto& x : t) if ((int)x.type >= 64 && (int)x.type > 64) { if (ri < rc) { x.raw_bits_count = s[off]; x.raw_bits = s[off+1] | (s[off+2]<<8); off += 3; ri++; } }
        CDFBuilder::cleanup(cdf);
        return t;
    }

    static std::vector<Token> decode_stream_parallel(const uint8_t* s, size_t sz, const PIndex& pi) {
        if (sz < 8) return {};
        uint32_t cs; std::memcpy(&cs, s, 4);
        std::vector<uint32_t> f(cs/4); std::memcpy(f.data(), s+4, cs);
        CDFTable cdf = CDFBuilder().build_from_freq(f);
        uint32_t tc; std::memcpy(&tc, s+4+cs, 4);
        uint32_t rs; std::memcpy(&rs, s+8+cs, 4);
        unsigned int nt = thread_budget::max_threads(8);
        auto stream_decode_tokens = thread_budget::ScopedThreadTokens::try_acquire_up_to(nt, 2);
        if (stream_decode_tokens.acquired()) nt = stream_decode_tokens.count();
        else nt = 1;
        auto syms = ParallelDecoder::decode(std::span<const uint8_t>(s+12+cs, rs), pi, cdf, nt);
        std::vector<Token> t; t.reserve(tc);
        for (int x : syms) t.emplace_back((TokenType)x, 0, 0);
        uint32_t rc; size_t off = 12+cs+rs; std::memcpy(&rc, s+off, 4); off += 4;
        size_t ri = 0; for (auto& x : t) if ((int)x.type >= 64 && (int)x.type > 64) { if (ri < rc) { x.raw_bits_count = s[off]; x.raw_bits = s[off+1] | (s[off+2]<<8); off += 3; ri++; } }
        CDFBuilder::cleanup(cdf);
        return t;
    }
