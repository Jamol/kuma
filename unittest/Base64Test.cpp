
#include <gtest/gtest.h>
#include "util/base64.h"
#include "util/defer.h"

#include <memory>
#include <array>

using namespace kuma;

const std::string encoded_str = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy8/T19vf4+fr7/P3+/wABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9AQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ent8fX5/gIGCg4SFhoeIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp+goaKjpKWmp6ipqqusra6vsLGys7S1tre4ubq7vL2+v8DBwsPExcbHyMnKy8zNzs/Q0dLT1NXW19jZ2tvc3d7f4OHi4+Tl5ufo6err7O3u7/Dx8vP09fb3+Pn6+/z9/v8AAQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyAhIiMkJSYnKCkqKywtLi8wMTIzNDU2Nzg5Og==";

class Base64_TEST : public ::testing::Test
{
public:
    virtual void SetUp()
    {
        for (size_t i=0; i < raw_buf_.size(); ++i) {
            raw_buf_[i] = i;
        }
    }
    
    virtual void TearDown()
    {
    }
    
protected:
    std::array<uint8_t, 571> raw_buf_;
};

TEST_F(Base64_TEST, Test_Encode_Decode_Buffer)
{
    auto calc_len = x64_calc_encode_buf_size(raw_buf_.size());
    std::vector<char> enc_buf(calc_len, '\0');
    auto enc_len = x64_encode(raw_buf_.data(), raw_buf_.size(), &enc_buf[0], enc_buf.size(), false);
    
    ASSERT_EQ(encoded_str.size(), enc_len);
    auto rv = memcmp(encoded_str.data(), &enc_buf[0], enc_len);
    ASSERT_EQ(0, rv);
    
    calc_len = x64_calc_decode_buf_size(enc_len);
    std::vector<uint8_t> dec_buf(calc_len, '\0');
    auto dec_len = x64_decode(&enc_buf[0], enc_len, &dec_buf[0], calc_len);
    
    ASSERT_EQ(raw_buf_.size(), dec_len);
    rv = memcmp(raw_buf_.data(), &dec_buf[0], dec_len);
    ASSERT_EQ(0, rv);
}

TEST_F(Base64_TEST, Test_Encode_Decode_String)
{
    auto enc_str = x64_encode(raw_buf_.data(), raw_buf_.size(), false);
    ASSERT_EQ(encoded_str, enc_str);
    
    auto dec_str = x64_decode(enc_str);
    ASSERT_EQ(raw_buf_.size(), dec_str.size());
    auto rv = memcmp(raw_buf_.data(), dec_str.data(), dec_str.size());
    ASSERT_EQ(0, rv);
}

TEST_F(Base64_TEST, Test_Encode_Decode_Context)
{
    auto *ctx = x64_ctx_create();
    DEFER([ctx]{
        x64_ctx_destroy(ctx);
    });
    
    auto calc_len = x64_calc_encode_buf_size(raw_buf_.size());
    std::vector<char> enc_buf(calc_len, '\0');
    
    auto *src = raw_buf_.data();
    size_t src_len = 124;
    auto *dst = enc_buf.data();
    size_t dst_len = enc_buf.size();
    auto enc_len = x64_ctx_encode(ctx, src, src_len, dst, dst_len, false, false);
    src += src_len;
    dst += enc_len;
    src_len = raw_buf_.size() - src_len - 1;
    dst_len = enc_buf.size() - enc_len;
    auto ret_len = x64_ctx_encode(ctx, src, src_len, dst, dst_len, false, false);
    enc_len += ret_len;
    src += src_len;
    dst += ret_len;
    src_len = 1;
    dst_len = enc_buf.size() - enc_len;
    enc_len += x64_ctx_encode(ctx, src, src_len, dst, dst_len, false, true);
    
    ASSERT_EQ(encoded_str.size(), enc_len);
    auto rv = memcmp(encoded_str.data(), &enc_buf[0], enc_len);
    ASSERT_EQ(0, rv);
    
    // decode
    x64_ctx_reset(ctx);
    calc_len = x64_calc_decode_buf_size(enc_len);
    std::vector<uint8_t> dec_buf(calc_len, '\0');
    
    auto *dsrc = enc_buf.data();
    size_t dsrc_len = 350;
    auto *ddst = dec_buf.data();
    size_t ddst_len = dec_buf.size();
    auto dec_len = x64_ctx_decode(ctx, dsrc, dsrc_len, ddst, ddst_len, false);
    dsrc += dsrc_len;
    ddst += dec_len;
    dsrc_len = enc_len - dsrc_len - 1;
    ddst_len = dec_buf.size() - dec_len;
    ret_len = x64_ctx_decode(ctx, dsrc, dsrc_len, ddst, ddst_len, false);
    dec_len += ret_len;
    dsrc += dsrc_len;
    ddst += ret_len;
    dsrc_len = 1;
    ddst_len = dec_buf.size() - dec_len;
    dec_len += x64_ctx_decode(ctx, dsrc, dsrc_len, ddst, ddst_len, false);
    
    ASSERT_EQ(raw_buf_.size(), dec_len);
    rv = memcmp(raw_buf_.data(), &dec_buf[0], dec_len);
    ASSERT_EQ(0, rv);
}
