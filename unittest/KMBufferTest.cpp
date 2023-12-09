
#include <gtest/gtest.h>
#include "kmbuffer.h"

using namespace kuma;

TEST(KMBufferTest, allocBuffer)
{
    const size_t kTestSize = 256*1024;
    KMBuffer buf(KMBuffer::StorageType::AUTO);
    auto ret = buf.allocBuffer(kTestSize);
    EXPECT_TRUE(ret);
    EXPECT_EQ(kTestSize, buf.space());
    EXPECT_EQ(0, buf.size());
    buf.bytesWritten(601);
    EXPECT_EQ(kTestSize - 601, buf.space());
    EXPECT_EQ(601, buf.size());
    buf.destroy();
}

TEST(KMBufferTest, Invalid_Size)
{
    auto deleter = [](void *buf, size_t) {
        char* ptr = static_cast<char*>(buf);
        delete [] ptr;
    };
    size_t str_size = 3000;
    size_t buf_size = 3300;
    auto *str = new char(str_size);
    KMBuffer buf(str, str_size, buf_size, 0, deleter);
    EXPECT_EQ(str_size, buf.size());
}

TEST(KMBufferTest, Invalid_Offset)
{
    auto deleter = [](void *buf, size_t) {
        char* ptr = static_cast<char*>(buf);
        delete [] ptr;
    };
    size_t str_size = 3000;
    size_t buf_size = 2300;
    size_t buf_off = 2000;
    auto *str = new char(str_size);
    auto *buf = new KMBuffer(str, str_size, buf_size, buf_off, deleter);
    EXPECT_EQ(1000, buf->size());
    delete buf;
}

TEST(KMBufferTest, Test_Chained_Buffer)
{
    char str1[1024] = {0};
    char str2[2048] = {0};
    memset(str1, 'A', sizeof(str1));
    memset(str2, 'B', sizeof(str2));
    
    auto *buf1 = new KMBuffer(str1, sizeof(str1), KMBuffer::StorageType::OTHER);
    size_t buf1_size = 500;
    buf1->bytesWritten(buf1_size);
    EXPECT_FALSE(buf1->isChained());
    EXPECT_EQ(buf1_size, buf1->size());
    EXPECT_EQ(buf1_size, buf1->chainLength());
    
    size_t buf2_size = 1920;
    KMBuffer buf2(str2, buf2_size);
    buf2.bytesWritten(buf2_size);
    EXPECT_FALSE(buf2.isChained());
    
    auto deleter = [](void *buf, size_t) {
        char* ptr = static_cast<char*>(buf);
        delete [] ptr;
    };
    size_t str3_size = 3000;
    size_t buf3_size = 2300;
    size_t buf3_off = 300;
    auto *str3 = new char[str3_size];
    auto *buf3 = new KMBuffer(str3, str3_size, buf3_size, buf3_off, deleter);
    
    auto deleter4 = [](void *buf, size_t) {};
    size_t str4_size = 3000;
    size_t buf4_size = 1300;
    size_t buf4_off = 0;
    auto *str4 = new char[str4_size];
    auto *buf4 = new KMBuffer(str4, str4_size, buf4_size, buf4_off, deleter4);
    
    buf1->append(&buf2);
    buf1->append(buf3);
    buf1->append(buf4);
    
    EXPECT_TRUE(buf1->isChained());
    auto orig_size = buf1->chainLength();
    EXPECT_EQ(buf1_size + buf2_size + buf3_size + buf4_size, orig_size);
    
    auto *sub_buf = buf1->subbuffer(500, 3481);
    ASSERT_NE(nullptr, sub_buf);
    EXPECT_EQ(3481, sub_buf->chainLength());
    
    auto *clone_buf = buf1->clone();
    ASSERT_NE(nullptr, clone_buf);
    EXPECT_EQ(buf1->chainLength(), clone_buf->chainLength());
    
    *clone_buf = *sub_buf;
    EXPECT_EQ(sub_buf->chainLength(), clone_buf->chainLength());
    clone_buf->destroy();
    
    delete sub_buf;
    
    size_t read_buf_size = 2481;
    char *read_buf = new char[2481];
    auto ret = buf1->readChained(read_buf, read_buf_size);
    EXPECT_EQ(read_buf_size, ret);
    EXPECT_EQ(orig_size - read_buf_size, buf1->chainLength());
    
    buf1->destroy();
    delete [] str4;
}

TEST(KMBufferTest, Verify_Sub_Buffer)
{
    char str1[1024] = {0};
    char str2[2048] = {0};
    memset(str1, 'A', sizeof(str1));
    memset(str2, 'B', sizeof(str2));
    
    auto *buf1 = new KMBuffer(str1, sizeof(str1), KMBuffer::StorageType::OTHER);
    size_t buf1_size = 500;
    buf1->bytesWritten(buf1_size);
    EXPECT_FALSE(buf1->isChained());
    EXPECT_EQ(buf1_size, buf1->size());
    EXPECT_EQ(buf1_size, buf1->chainLength());
    
    size_t buf2_size = 1920;
    KMBuffer buf2(str2, buf2_size);
    buf2.bytesWritten(buf2_size);
    EXPECT_FALSE(buf2.isChained());
    
    auto deleter = [](void *buf, size_t) {
        char* ptr = static_cast<char*>(buf);
        delete [] ptr;
    };
    size_t str3_size = 3000;
    size_t buf3_size = 2300;
    size_t buf3_off = 300;
    auto *str3 = new char[str3_size];
    memset(str3, 'C', str3_size);
    auto *buf3 = new KMBuffer(str3, str3_size, buf3_size, buf3_off, deleter);
    
    buf2.append(buf1);
    buf2.append(buf3);
    
    EXPECT_TRUE(buf2.isChained());
    auto orig_size = buf2.chainLength();
    EXPECT_EQ(buf1_size + buf2_size + buf3_size, orig_size);
    
    size_t sub_size = 3481;
    size_t sub_offset = 300;
    auto *sub_buf = buf2.subbuffer(sub_offset, sub_size);
    ASSERT_NE(nullptr, sub_buf);
    EXPECT_EQ(sub_size, sub_buf->chainLength());
    
    char *read_buf = new char[sub_size];
    sub_buf->readChained(read_buf, sub_size);
    int pos = 0;
    for (; pos<buf2_size-sub_offset; ++pos) {
        EXPECT_EQ('B', read_buf[pos]);
    }
    for (; pos<buf2_size-sub_offset+buf1_size; ++pos) {
        EXPECT_EQ('A', read_buf[pos]);
    }
    for (; pos<sub_size; ++pos) {
        EXPECT_EQ('C', read_buf[pos]);
    }
    
    delete sub_buf;
    buf2.destroy();
    
    delete [] read_buf;
}

TEST(KMBufferTest, Test_Read_and_Const_Read)
{
    KMBuffer buf1(KMBuffer::StorageType::AUTO);
    std::allocator<char> a;
    buf1.allocBuffer(1024, a);
    buf1.bytesWritten(1024);
    
    EXPECT_EQ(1024, buf1.length());
    
    char d2[256];
    KMBuffer buf2(d2, sizeof(d2), 128);
    EXPECT_EQ(128, buf2.length());
    
    char d3[256];
    KMBuffer buf3(d3, sizeof(d3), sizeof(d3));
    EXPECT_EQ(sizeof(d3), buf3.length());
    
    buf1.append(&buf2);
    EXPECT_EQ(1024+128, buf1.chainLength());
    buf1.append(&buf3);
    EXPECT_EQ(1024+128+256, buf1.chainLength());
    
    auto const *kmb = &buf1;
    std::vector<char> vec;
    vec.resize(buf1.chainLength());
    auto ret = kmb->readChained(&vec[0], vec.size());
    EXPECT_EQ(vec.size(), ret);
    EXPECT_EQ(vec.size(), buf1.chainLength());
    
    ret = buf1.readChained(&vec[0], vec.size());
    EXPECT_EQ(vec.size(), ret);
    EXPECT_EQ(0, buf1.chainLength());
}

TEST(KMBufferTest, Test_Destroy)
{
    KMBuffer buf1(KMBuffer::StorageType::AUTO);
    std::allocator<char> a;
    buf1.allocBuffer(1024, a);
    buf1.bytesWritten(1024);
    
    char d2[256];
    KMBuffer buf2(d2, sizeof(d2), 128);
    
    char d3[256];
    KMBuffer buf3(d3, sizeof(d3), sizeof(d3));
    
    buf1.append(&buf2);
    EXPECT_EQ(1024+128, buf1.chainLength());
    buf1.append(&buf3);
    EXPECT_EQ(1024+128+256, buf1.chainLength());
    
    buf2.destroy();
    EXPECT_FALSE(buf2.isChained());
    EXPECT_EQ(0, buf2.length());
    EXPECT_EQ(1024+256, buf1.chainLength());
    buf1.destroy();
    EXPECT_FALSE(buf1.isChained());
    EXPECT_EQ(0, buf1.length());
    EXPECT_FALSE(buf3.isChained());
    EXPECT_EQ(0, buf3.length());
}

TEST(KMBufferTest, Test_Unlink)
{
    KMBuffer buf1(KMBuffer::StorageType::AUTO);
    std::allocator<char> a;
    buf1.allocBuffer(1024, a);
    buf1.bytesWritten(1024);
    
    char d2[256];
    KMBuffer buf2(d2, sizeof(d2), 128);
    
    char d3[256];
    KMBuffer buf3(d3, sizeof(d3), sizeof(d3));
    
    buf1.append(&buf2);
    EXPECT_EQ(1024+128, buf1.chainLength());
    buf1.append(&buf3);
    EXPECT_EQ(1024+128+256, buf1.chainLength());
    
    buf2.unlink();
    EXPECT_FALSE(buf2.isChained());
    EXPECT_EQ(128, buf2.length());
    EXPECT_EQ(1024+256, buf1.chainLength());
    buf1.unlink();
    EXPECT_FALSE(buf1.isChained());
    EXPECT_EQ(1024, buf1.length());
    EXPECT_FALSE(buf3.isChained());
    EXPECT_EQ(256, buf3.length());
}
