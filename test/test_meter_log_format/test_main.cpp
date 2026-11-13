#include <meter_log_format.h>
#include <meter_log_io.h>
#include <cmath>
#include <string>
#include <vector>
#include <cstdio>
#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#include <signal.h>
#endif
#ifdef METER_LOG_FORMAT_STANDALONE
#include <cstdio>
#include <cstdlib>
#define TEST_ASSERT_TRUE(v) do {if(!(v)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);std::exit(1);}} while(0)
#define TEST_ASSERT_FALSE(v) TEST_ASSERT_TRUE(!(v))
#define TEST_ASSERT_EQUAL_STRING(a,b) TEST_ASSERT_TRUE(std::string(a)==std::string(b))
#define TEST_ASSERT_EQUAL_UINT(a,b) TEST_ASSERT_TRUE((a)==(b))
#define RUN_TEST(f) do {f();std::printf("PASS %s\n",#f);} while(0)
#else
#include <unity.h>
#endif
using namespace meter_log_format;
void setUp() {}
void tearDown() {}

// CSV parser is independent of the writer, so schema/quoting checks exercise
// the actual exported result instead of duplicating its assembly logic.
static std::vector<std::string> cells(const std::string &csv) {
    std::vector<std::string> out;
    std::string cell;bool quoted=false;
    for(size_t i=0;i<csv.size();++i) {
        char c=csv[i];
        if(c=='"') {if(quoted&&i+1<csv.size()&&csv[i+1]=='"'){cell+='"';++i;}else quoted=!quoted;}
        else if(c==','&&!quoted){out.push_back(cell);cell.clear();}
        else if(c=='\r'&&!quoted&&i+1<csv.size()&&csv[i+1]=='\n'){out.push_back(cell);return out;}
        else cell+=c;
    }
    out.push_back(cell);return out;
}
static std::string field(const Row &r,const char *key) {
    const auto names=cells(csvHeader()),values=cells(csvRow(r));
    TEST_ASSERT_EQUAL_UINT(names.size(),values.size());
    for(size_t i=0;i<names.size();++i)if(names[i]==key)return values[i];
    TEST_ASSERT_TRUE(false);return {};
}
void test_generated_names_and_traversal_boundary() {
    TEST_ASSERT_EQUAL_STRING("meter-A1B2C3D4E5F60708-7.csv",fileName("A1B2C3D4E5F60708-7").c_str());
    TEST_ASSERT_TRUE(validLogName("meter-A1B2C3D4E5F60708-7.csv"));
    for(const char *bad:{"../meter-A1B2C3D4E5F60708-7.csv","/meter-A1B2C3D4E5F60708-7.csv","meter-A1B2C3D4E5F60708-7.csv/x","meter-A1B2C3D4E5F60708-7.csv\\x","meter-A1B2C3D4E5F60708-7.csv%00","meter-A1B2C3D4E5F60708-7.CSV","meter-xyz-7.csv","meter-A1B2C3D4E5F60708-0.csv","meter-A1B2C3D4E5F60708-01.csv","meter-A1B2C3D4E5F60708-4294967296.csv"})
        TEST_ASSERT_FALSE(validLogName(bad));
    TEST_ASSERT_TRUE(fileName("../../bad").empty());
    TEST_ASSERT_TRUE(fileName("").empty());
}
void test_operation_ids_are_bounded_unambiguous_ascii_tokens() {
    TEST_ASSERT_TRUE(validOperationId("format-1234567890_A"));
    TEST_ASSERT_TRUE(validOperationId(std::string(64,'a')));
    for(const std::string &bad:{std::string(""),std::string(15,'a'),std::string(65,'a'),std::string("format/1234567890a"),std::string("format:1234567890a"),std::string("format 1234567890a"),std::string("format\n1234567890a"),std::string("format-123456789\xc3\xa9")})
        TEST_ASSERT_FALSE(validOperationId(bad));
}
void test_text_roundtrip_and_spreadsheet_formula_protection() {
    TEST_ASSERT_EQUAL_STRING("\"He said \"\"V\"\", then\r\nleft\"",csvCell("He said \"V\", then\r\nleft").c_str());
    for(const char *formula:{"=HYPERLINK(\"x\")","  +1","-2","@SUM(A1)","\ttext","\rtext","\ntext"}) {
        const auto parsed=cells(csvCell(formula)+"\r\n");
        TEST_ASSERT_EQUAL_STRING((std::string("'")+formula).c_str(),parsed[0].c_str());
    }
    TEST_ASSERT_EQUAL_STRING("\"Voltage - good\"",csvCell("Voltage - good").c_str());
}
void test_invalid_readings_are_blank_and_numeric_is_truthful() {
    Row r;r.reading=true;r.numeric=false;r.value=0;r.text="OL";
    TEST_ASSERT_EQUAL_STRING("",field(r,"value").c_str());
    TEST_ASSERT_EQUAL_STRING("false",field(r,"numeric").c_str());
    TEST_ASSERT_EQUAL_STRING("OL",field(r,"text").c_str());
    r.numeric=true;r.value=NAN;TEST_ASSERT_EQUAL_STRING("",field(r,"value").c_str());
    TEST_ASSERT_EQUAL_STRING("false",field(r,"numeric").c_str());
    r.value=INFINITY;TEST_ASSERT_EQUAL_STRING("",field(r,"value").c_str());
    r.value=-1.25;TEST_ASSERT_EQUAL_STRING("-1.25",field(r,"value").c_str());
    TEST_ASSERT_EQUAL_STRING("true",field(r,"numeric").c_str());
    r.reading=false;TEST_ASSERT_EQUAL_STRING("",field(r,"value").c_str());
}
void test_identity_time_session_and_gaps_survive_export() {
    Row r;r.seq=9007199254740993ULL;r.uptimeMs=42000;r.timestampMs=0;
    r.deviceId="983DAEE91834";r.bootId="A1B2C3D4E5F60708";r.sessionId=r.bootId+"-7";
    r.label="bench, load";r.event="mark";r.message="=danger";r.ruleId=4;r.code=-3;r.success=false;r.droppedBefore=9;
    TEST_ASSERT_EQUAL_STRING("9007199254740993",field(r,"seq").c_str());
    TEST_ASSERT_EQUAL_STRING("",field(r,"timestampMs").c_str());
    TEST_ASSERT_EQUAL_STRING("42000",field(r,"uptimeMs").c_str());
    TEST_ASSERT_EQUAL_STRING("983DAEE91834",field(r,"deviceId").c_str());
    TEST_ASSERT_EQUAL_STRING("A1B2C3D4E5F60708-7",field(r,"sessionId").c_str());
    TEST_ASSERT_EQUAL_STRING("bench, load",field(r,"label").c_str());
    TEST_ASSERT_EQUAL_STRING("'=danger",field(r,"message").c_str());
    TEST_ASSERT_EQUAL_STRING("9",field(r,"droppedBefore").c_str());
    TEST_ASSERT_EQUAL_STRING("-3",field(r,"code").c_str());
    TEST_ASSERT_EQUAL_STRING("event",field(r,"kind").c_str());
    r.timestampMs=1794345600123ULL;TEST_ASSERT_EQUAL_STRING("1794345600123",field(r,"timestampMs").c_str());
}
void test_checked_flush_observes_real_buffered_write_failure() {
    int ends[2];
#if defined(_WIN32)
    TEST_ASSERT_TRUE(_pipe(ends,4096,_O_BINARY)==0);
    FILE *stream=_fdopen(ends[1],"wb");TEST_ASSERT_TRUE(stream!=nullptr);
    TEST_ASSERT_TRUE(_close(ends[0])==0);
#else
    signal(SIGPIPE,SIG_IGN);TEST_ASSERT_TRUE(pipe(ends)==0);
    FILE *stream=fdopen(ends[1],"wb");TEST_ASSERT_TRUE(stream!=nullptr);
    TEST_ASSERT_TRUE(close(ends[0])==0);
#endif
    char buffer[4096];TEST_ASSERT_TRUE(setvbuf(stream,buffer,_IOFBF,sizeof(buffer))==0);
    TEST_ASSERT_TRUE(fwrite("row",1,3,stream)==3);
    TEST_ASSERT_TRUE(meter_log_io::checkedFlush(stream)==meter_log_io::FlushResult::FlushError);
    fclose(stream);
    FILE *normal=tmpfile();TEST_ASSERT_TRUE(normal!=nullptr);
    TEST_ASSERT_TRUE(fwrite("row",1,3,normal)==3);
    TEST_ASSERT_TRUE(meter_log_io::checkedFlush(normal)==meter_log_io::FlushResult::Ok);
    TEST_ASSERT_TRUE(fclose(normal)==0);
}
int main() {
#ifndef METER_LOG_FORMAT_STANDALONE
    UNITY_BEGIN();
#endif
    RUN_TEST(test_generated_names_and_traversal_boundary);
    RUN_TEST(test_operation_ids_are_bounded_unambiguous_ascii_tokens);
    RUN_TEST(test_text_roundtrip_and_spreadsheet_formula_protection);
    RUN_TEST(test_invalid_readings_are_blank_and_numeric_is_truthful);
    RUN_TEST(test_identity_time_session_and_gaps_survive_export);
    RUN_TEST(test_checked_flush_observes_real_buffered_write_failure);
#ifndef METER_LOG_FORMAT_STANDALONE
    return UNITY_END();
#endif
    return 0;
}
