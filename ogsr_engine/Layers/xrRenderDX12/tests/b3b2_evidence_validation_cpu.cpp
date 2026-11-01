#include "b3b2_persistent_evidence_validation.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace b3b2evidence;
static const std::string sha(64, 'a');
static const std::string sha2(64, 'b');
static std::string V2Record(int severity, uint64_t id,
                            const std::string& payload)
{
    const std::string hex = HexEncodeDescription(payload.data(), payload.size());
    return "format=v2 index=0 severity=" + std::to_string(severity) +
        " id=" + std::to_string(id) +
        " description_length=" + std::to_string(payload.size()) +
        " description_hex=" + hex + "\n";
}
static std::string Line(uint64_t i, bool sample, const char* kind = "offline")
{
    std::string status = sample ? "VISIBLE_DRAW_VERIFIED" : "GPU_COMPLETED_NOT_READBACK_VERIFIED";
    const char* bbox = sample ? "\"0,0..10,10\"" : "\"0,0..0,0\"";
    const char* depth = sample ? "0.01,1" : "1,1";
    return "rid," + std::to_string(i) + "," + kind + ",1,1,1,1,1,0123456789ABCDEF," + sha +
        ",1280x720,\"(1,2,3)\",11,12,13,14,15,16,17," + std::to_string(i+1) + "," +
        (sample ? "1" : "0") + "," + status + "," + (sample ? "23" : "0") + "," + bbox +
        "," + depth + "," + status + "\n";
}
int main()
{
    int checks = 0;
    auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
        ++checks;
    };
    CsvEvidence csv;
    const std::string good = std::string(kCsvHeader) + "\n" + Line(1,true) + Line(2,false);
    check(CsvValid(good,"rid",2,csv) && csv.frames==2 && csv.sampled==1 && csv.visible==1, "csv-pass");
    auto nulAge = good;
    auto nulAgePos = nulAge.find(",11,");
    nulAge.replace(nulAgePos, 4, std::string(",-1\0x,", 7));
    check(!CsvValid(nulAge,"rid",2,csv), "csv-nul-number");
    auto negativeDepth = good;
    auto depthPos = negativeDepth.find(",0.01,1,");
    negativeDepth.replace(depthPos, 8, ",-1,2,");
    check(!CsvValid(negativeDepth,"rid",2,csv), "csv-negative-depth");
    auto hexAge = good;
    auto hexAgePos = hexAge.find(",11,");
    hexAge.replace(hexAgePos, 4, ",0x1p4,");
    check(!CsvValid(hexAge,"rid",2,csv), "csv-hex-number");
    auto badViewport = good;
    badViewport.replace(badViewport.find("1280x720"), 8, "1280x");
    check(!CsvValid(badViewport,"rid",2,csv), "csv-bad-viewport");
    auto badPosition = good;
    badPosition.replace(badPosition.find("\"(1,2,3)\""), 9, "\"(1,2)\"");
    check(!CsvValid(badPosition,"rid",2,csv), "csv-bad-position");
    auto badCameraHash = good;
    badCameraHash.replace(badCameraHash.find(",0123456789ABCDEF,"), 18, ",xyz,");
    check(!CsvValid(badCameraHash,"rid",2,csv), "csv-bad-camera-hash");
    check(!CsvValid(std::string(kCsvHeader)+"\nrid,\n"+Line(2,false),"rid",2,csv), "truncated-record");
    check(!CsvValid(std::string(kCsvHeader)+"\n"+Line(1,true)+Line(1,false),"rid",2,csv), "duplicate-index");
    check(!CsvValid(std::string(kCsvHeader)+"\n"+Line(1,true)+Line(2,false).substr(0,20),"rid",2,csv), "missing-cols");
    auto broken = good; auto position=broken.rfind("GPU_COMPLETED_NOT_READBACK_VERIFIED");
    broken.replace(position, std::string("GPU_COMPLETED_NOT_READBACK_VERIFIED").size(), "FAIL");
    check(!CsvValid(broken,"rid",2,csv), "csv-status-fail");
    check(CsvValid(std::string(kCsvHeader)+"\n"+Line(2,false)+Line(1,true),"rid",2,csv), "unordered-valid");
    check(CsvValid(std::string(kCsvHeader)+"\n"+Line(1,false)+Line(2,true,"last"),"rid",2,csv) && csv.lastFrames==1,"last-sample-valid");
    std::map<uint64_t,SampleEntry> entries;
    const auto record=[&](uint64_t n){return "frame="+std::to_string(n)+" color_sha256="+sha+" depth_sha256="+sha2+" meta_sha256="+sha+"\n";};
    const std::string manifest="run_id=rid\n"+record(1)+record(2);
    check(SampleManifest(manifest,"rid",2,entries) && entries.size()==2,"manifest-valid");
    check(!SampleManifest("run_id=rid\n","rid",2,entries),"manifest-empty");
    check(!SampleManifest("run_id=rid\n"+record(1),"rid",2,entries),"manifest-truncated");
    check(!SampleManifest("run_id=rid\n"+record(1)+record(1),"rid",2,entries),"manifest-duplicate");
    uint32_t w=0,h=0;
    std::string meta="run_id=rid\nframe_index=1\nlevel_session=1\ncapture_epoch=1\ncapture_sequence=1\nsource_game_frame=1\nviewport=1280x720\ncamera_hash=0123456789ABCDEF\ncamera_sha256="+sha+"\nfence_value=2\ngpu_status=GPU_COMPLETED\ncoverage_status=VISIBLE_DRAW_VERIFIED\nnon_clear_pixels=23\nbbox=0,0..10,10\ndepth_min=0.01\ndepth_max=1\n";
    check(SampleMetadata(meta,"rid",1,w,h)&&w==1280&&h==720,"meta-valid");
    check(!SampleMetadata(meta,"rid",2,w,h),"meta-frame-id");
    check(CsvValid(good,"rid",2,csv) && csv.samples.size()==1 &&
          SampleMetadata(meta,"rid",1,w,h,&csv.samples[0]),"csv-sampled-meta-linked");
    auto badMeta=meta;
    auto hashPos=badMeta.find("camera_hash=0123456789ABCDEF");
    badMeta.replace(hashPos, 16, "camera_hash=ffff");
    check(!SampleMetadata(badMeta,"rid",1,w,h,&csv.samples[0]),"csv-camera-hash-mismatch");
    badMeta=meta;
    auto fencePos=badMeta.find("fence_value=2");
    badMeta.replace(fencePos, 13, "fence_value=3");
    check(!SampleMetadata(badMeta,"rid",1,w,h,&csv.samples[0]),"csv-fence-mismatch");
    badMeta=meta;
    auto metaDepthPos=badMeta.find("depth_max=1");
    badMeta.replace(metaDepthPos, 11, "depth_max=0.9");
    check(!SampleMetadata(badMeta,"rid",1,w,h,&csv.samples[0]),"csv-depth-mismatch");
    Fields f;
    check(ParseFields("validation_status=PASS\ncompletion_confirmed=1\n",f),"kv-valid");
    check(!ParseFields("validation_status=PASS\nvalidation_status=FAIL\n",f),"kv-duplicate");
    check(ParseFields("debug_layer_available=1\ndebug_layer_available=1\n",f,true),"kv-redundant-capability");
    check(!ParseFields("debug_layer_available=1\ndebug_layer_available=0\n",f,true),"kv-capability-mismatch");
    uint64_t parsed = 0;
    check(!Unsigned("1junk",parsed),"unsigned-tail");
    const std::string log="fence stage=geometry upload expected=1 completed=1 signal=1 arm=1 wait=1 removed=0x00000000 outcome=Completed confirmed=1\n"
        "fence stage=camera frame expected=2 completed=2 signal=1 arm=1 wait=1 removed=0x00000000 outcome=Completed confirmed=1\n";
    check(ValidFenceLog(log,1),"fence-confirmed");
    auto invalid=log; invalid.replace(invalid.find("confirmed=1"),11,"confirmed=0");
    check(!ValidFenceLog(invalid,1),"fence-unconfirmed");
    check(ValidInfoQueue("index=0 severity=3 id=1016 description_length=20 description=GPU-Based Validation\n"),"infoqueue-info");
    check(!ValidInfoQueue("index=0 severity=1 id=4 description_length=3 description=ERR\n"),"infoqueue-error");
    check(!ValidInfoQueue("index=0 severity=0 id=4 description_length=3 description=BAD\n"),"infoqueue-corruption");
    Fields v;
    check(ParseFields("validation_status=PASS\ndevice_removal_reason=0x00000000\ninfoqueue_available=1\ndebug_layer_available=1\ngbv_active=1\ncompletion_confirmed=1\ninfoqueue_error=0\ninfoqueue_corruption=0\ninfoqueue_warning=0\ninfoqueue_discarded=0\ninfoqueue_hidden_after_baseline=0\ninfoqueue_retrieval_failures=0\ninfoqueue_stored=1\ninfoqueue_enumerated=1\n",v) && ValidValidation(v),"validation-pass");
    v["validation_status"]="FAIL";
    check(!ValidValidation(v),"validation-status-fail");
    v["validation_status"]="PASS"; v["infoqueue_error"]="1";
    check(!ValidValidation(v),"validation-error-fail");
    v["infoqueue_error"]="0"; v["completion_confirmed"]="0";
    check(!ValidValidation(v),"validation-fence-fail");
    Fields c;
    check(ParseFields("frames_completed=2\nframes_submitted=2\nframes_received=2\nframes_sampled=1\nframes_failed=0\ndevice_create_count=1\nqueue_create_count=1\npso_create_count=1\ngeometry_upload_count=1\nvertex_buffer_create_count=1\nindex_buffer_create_count=1\n",c) && ValidCounters(c,nullptr,2,1),"counters-pass");
    c["frames_completed"]="0";
    check(!ValidCounters(c,nullptr,2,1),"counters-rejected");

    // F2: writer canonical 16-char output and legacy short uint64 hashes are
    // the same numeric identity; malformed/overflow values are not.
    auto shortHash = good;
    shortHash.replace(shortHash.find(",0123456789ABCDEF,"), 18, ",123456789ABCDEF,");
    check(CsvValid(shortHash,"rid",2,csv), "csv-short-hash-accepted");
    auto overflowHash = good;
    overflowHash.replace(overflowHash.find(",0123456789ABCDEF,"), 18, ",10000000000000000,");
    check(!CsvValid(overflowHash,"rid",2,csv), "csv-hash-overflow-rejected");
    auto prefixedHash = good;
    prefixedHash.replace(prefixedHash.find(",0123456789ABCDEF,"), 18, ",0x123456789ABCDE,");
    check(!CsvValid(prefixedHash,"rid",2,csv), "csv-hash-prefix-rejected");
    auto shortMeta = meta;
    shortMeta.replace(shortMeta.find("camera_hash=0123456789ABCDEF"), 28,
                      "camera_hash=123456789ABCDEF");
    check(CsvValid(shortHash,"rid",2,csv) && csv.samples.size()==1 &&
          SampleMetadata(shortMeta,"rid",1,w,h,&csv.samples[0]),
          "short-camera-hash-metadata");

    // F3: UINT64_MAX is the device-removed sentinel, never a completion.
    auto sentinel = log;
    sentinel.replace(sentinel.find("completed=1"), 11,
                     "completed=18446744073709551615");
    check(!ValidFenceLog(sentinel,1), "fence-removed-sentinel-rejected");
    auto future = log;
    future.replace(future.find("completed=1"), 11, "completed=3");
    check(!ValidFenceLog(future,1), "fence-out-of-range-rejected");
    // R6: parsing and acceptance are intentionally separate. A structurally
    // valid WARNING record is parsed and counted, but the zero-warning
    // acceptance policy rejects it.
    InfoQueueCounts warningCounts;
    check(ParseInfoQueue("index=0 severity=2 id=7 description_length=4 description=warn\n",
                         warningCounts) && warningCounts.warning == 1 &&
          warningCounts.total == 1 && warningCounts.info == 0,
          "infoqueue-warning-parsed");
    check(!ValidInfoQueue("index=0 severity=2 id=7 description_length=4 description=warn\n",
                          &warningCounts) && warningCounts.warning == 1,
          "infoqueue-warning-rejected-by-policy");
    check(!ValidInfoQueue("index=1 severity=3 id=7 description_length=4 description=info\n",
                          nullptr), "infoqueue-index-sequence");
    check(!ValidInfoQueue(
              "index=0 severity=3 severity=2 id=7 description_length=4 description=warn\n",
              nullptr), "infoqueue-duplicate-severity-rejected");
    check(!ValidInfoQueue(
              "index=0 index=99 severity=3 id=7 description_length=4 description=warn\n",
              nullptr), "infoqueue-duplicate-index-rejected");
    check(ValidInfoQueue(
              "index=0 severity=3 id=7 description_length=15 description=text severity=2\n",
              nullptr), "infoqueue-description-field-words-allowed");
    check(!ValidInfoQueue(
              "index=0 severity=2 severity=3 id=7 description_length=4 description=warn\n",
              nullptr), "infoqueue-reversed-duplicate-severity-rejected");
    check(!ValidInfoQueue(
              "index=0 severity=3 id=7 id=8 description_length=4 description=info\n",
              nullptr), "infoqueue-duplicate-id-rejected");
    check(!ValidInfoQueue(
              "index=0 severity=3 id=7 description_length=4 description_length=4 description=info\n",
              nullptr), "infoqueue-duplicate-description-length-rejected");
    check(!ValidInfoQueue(
              "index=0 severity=3 id= description_length=4 description=info\n",
              nullptr), "infoqueue-empty-id-rejected");
    check(!ValidInfoQueue(
              "index=0 severity=3 id=7 description_length=4 description=info\nseverity=2\n",
              nullptr), "infoqueue-stray-structured-line-rejected");
    check(ValidInfoQueue(
              "index=0\tseverity=3\tid=7\tdescription_length=4 description=info\n",
              nullptr), "infoqueue-tabs-allowed");
    check(ValidInfoQueue(
              "index=0 severity=3 id=7 description_length=4 description=info\r\n",
              nullptr), "infoqueue-crlf-allowed");

    // R4-N1: v2 is one physical record per message and hex-encodes exact
    // description bytes, including the API terminal NUL.
    const std::string lfPayload = std::string("first line\nsecond line") + '\0';
    check(ValidInfoQueue(V2Record(3, 1, lfPayload), nullptr),
          "infoqueue-v2-lf");
    check(ValidInfoQueue(V2Record(3, 1,
              std::string("first line\r\nsecond line") + '\0'), nullptr),
          "infoqueue-v2-crlf");
    check(ValidInfoQueue(V2Record(3, 1,
              std::string("single line") + '\0'), nullptr),
          "infoqueue-v2-single-line");
    check(ValidInfoQueue(V2Record(3, 1,
              std::string("tab\tbackslash\\") + '\0'), nullptr),
          "infoqueue-v2-tab-backslash");
    check(ValidInfoQueue(V2Record(3, 1,
              std::string("text severity=2 index=99 description_length=0") + '\0'),
              nullptr), "infoqueue-v2-key-like-text");
    check(ValidInfoQueue(V2Record(3, 1, std::string(1, '\0')), nullptr),
          "infoqueue-v2-empty-description");
    InfoQueueCounts severityCounts;
    check(ParseInfoQueue(V2Record(2, 1,
              std::string("WARNING\nline") + '\0'), severityCounts) &&
          severityCounts.warning == 1 && severityCounts.total == 1 &&
          !ValidInfoQueue(V2Record(2, 1,
              std::string("WARNING\nline") + '\0'), nullptr),
          "infoqueue-v2-warning-lf");
    check(ParseInfoQueue(V2Record(1, 1,
              std::string("ERROR\r\nline") + '\0'), severityCounts) &&
          severityCounts.error == 1 && severityCounts.total == 1 &&
          !ValidInfoQueue(V2Record(1, 1,
              std::string("ERROR\r\nline") + '\0'), nullptr),
          "infoqueue-v2-error-crlf");
    // R5-N1: the description is arbitrary text. The structured severity is
    // INFO, so a negated ERROR must not be treated as an error message.
    check(ParseInfoQueue(V2Record(3, 1,
              std::string("No ERROR occurred") + '\0'), severityCounts) &&
          severityCounts.info == 1 && severityCounts.error == 0 &&
          ValidInfoQueue(V2Record(3, 1,
              std::string("No ERROR occurred") + '\0'), nullptr),
          "infoqueue-v2-no-error-accepted");
    check(!ValidInfoQueue(V2Record(3, 1, lfPayload) + "second line\n",
              nullptr), "infoqueue-v2-continuation-rejected");
    check(!ValidInfoQueue(
              "index=0 severity=3 id=1 description_length=23 description=first line\nsecond line\n",
              nullptr), "infoqueue-v1-multiline-rejected");
    const std::string utf8Payload =
        std::string("\xD0\x9F\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB5\xD1\x80\xD0\xBA\xD0\xB0") + '\0';
    check(ValidInfoQueue(V2Record(3, 1, utf8Payload), nullptr),
          "infoqueue-v2-utf8-bytes");
    std::string maxPayload(kMaxInfoQueueDescriptionBytes - 1, 'x');
    maxPayload.push_back('\0');
    check(ValidInfoQueue(V2Record(3, 1, maxPayload), nullptr),
          "infoqueue-v2-max-description");
    std::string oversizedPayload(kMaxInfoQueueDescriptionBytes, 'x');
    oversizedPayload.push_back('\0');
    check(!ValidInfoQueue(V2Record(3, 1, oversizedPayload), nullptr),
          "infoqueue-v2-oversized-description");
    check(!ValidInfoQueue(
              "format=v2 index=0 severity=3 id=1 description_length=2 description_hex=0\n",
              nullptr), "infoqueue-v2-odd-hex");
    check(!ValidInfoQueue(
              "format=v2 index=0 severity=3 id=1 description_length=2 description_hex=zz\n",
              nullptr), "infoqueue-v2-invalid-hex");
    check(!ValidInfoQueue(
              "format=v2 index=0 severity=3 id=1 description_length=2 description_hex=00\n",
              nullptr), "infoqueue-v2-short-payload");
    check(!ValidInfoQueue(
              "format=v2 index=0 severity=3 id=1 description_length=1 description_hex=0000\n",
              nullptr), "infoqueue-v2-long-payload");
    check(!ValidInfoQueue(
              "format=v2 index=0 severity=3 severity=2 id=1 description_length=1 description_hex=00\n",
              nullptr), "infoqueue-v2-duplicate-severity");
    check(!ValidInfoQueue(
              "format=v2 index=0 index=1 severity=3 id=1 description_length=1 description_hex=00\n",
              nullptr), "infoqueue-v2-duplicate-index");
    check(!ValidInfoQueue(
              "format=v2 index=0 severity=3 id=1 id=2 description_length=1 description_hex=00\n",
              nullptr), "infoqueue-v2-duplicate-id");
    check(!ValidInfoQueue(
              "format=v2 index=0 severity=3 id=1 description_length=1 description_length=1 description_hex=00\n",
              nullptr), "infoqueue-v2-duplicate-description-length");
    check(!ValidInfoQueue(
              "format=v2 index=0 severity=3 id=1 description_length=1 description_hex=00 extra=1\n",
              nullptr), "infoqueue-v2-extra-field");
    check(!ValidInfoQueue(
              "format=v2 index=0 severity=3 description_length=1 description_hex=00\n",
              nullptr), "infoqueue-v2-missing-id");
    std::string embeddedNul("a\0b", 3);
    embeddedNul.push_back('\0');
    check(!ValidInfoQueue(V2Record(3, 1, embeddedNul), nullptr),
          "infoqueue-v2-embedded-nul");

    // F4: control characters, negative underflow and reversed QPC are rejected.
    auto bboxNul = good;
    const size_t bboxPos = bboxNul.find("\"0,0..10,10\"");
    bboxNul.replace(bboxPos, 12, std::string("\"x\0y\"", 5));
    check(!CsvValid(bboxNul,"rid",2,csv), "csv-bbox-nul-rejected");
    auto underflow = good;
    underflow.replace(underflow.find(",14,"), 4, ",-1e-9999,");
    check(!CsvValid(underflow,"rid",2,csv), "csv-negative-underflow-rejected");
    auto qpcZero = good;
    qpcZero.replace(qpcZero.find(",13,"), 4, ",0,");
    check(!CsvValid(qpcZero,"rid",2,csv), "csv-reversed-qpc-rejected");

    // F2: canonical and historical short camera hashes are the same uint64.
    uint64_t hashValue = 0;
    check(HexU64("02707c72ae01af16", hashValue), "hex16-accepted");
    check(HexU64("2707c72ae01af16", hashValue), "hex-short-accepted");
    check(!HexU64("102707c72ae01af16", hashValue), "hex-overflow-rejected");
    check(!HexU64("0x2707c72ae01af16", hashValue), "hex-prefix-rejected");

    // R6 acceptance-contract decision for the former F3 negative case:
    // "index=0 severity=3 ... description=WARNING: synthetic" used to be
    // rejected because the raw text contained WARNING. That was a text
    // heuristic, not a D3D12 severity check. The same bytes now parse as one
    // INFO record; the zero-warning policy is satisfied because the structured
    // severity is INFO. A real WARNING is covered by severity=2 tests above.
    // Tampering with both mutable projections (per-message dump and aggregate
    // gpu-validation counters) is outside this trust model and is documented.
    check(ParseInfoQueue(
              "index=0 severity=3 id=7 description_length=18 description=WARNING: synthetic\n",
              severityCounts) && severityCounts.info == 1 &&
          severityCounts.warning == 0 && severityCounts.total == 1,
          "infoqueue-raw-warning-text-parsed-as-info");
    check(ValidInfoQueue(
              "index=0 severity=3 id=7 description_length=18 description=WARNING: synthetic\n",
              nullptr), "infoqueue-raw-warning-text-accepted-as-info");
    check(ParseInfoQueue(
              V2Record(2, 7, std::string("WARNING: synthetic") + '\0'),
              severityCounts) && severityCounts.warning == 1 &&
          !ValidInfoQueue(
              V2Record(2, 7, std::string("WARNING: synthetic") + '\0'),
              nullptr), "infoqueue-structured-warning-rejected");

    // R6: mandatory structured-severity matrix at the parser/policy boundary.
    // INFO and MESSAGE descriptions are arbitrary bytes. None of these cases
    // may be reclassified from words in the description.
    auto infoTextAccepted = [&](const std::string& description) {
        const std::string record = V2Record(3, 1, description + '\0');
        InfoQueueCounts parsed;
        return ParseInfoQueue(record, parsed) && parsed.info == 1 &&
               parsed.warning == 0 && parsed.error == 0 &&
               parsed.corruption == 0 && parsed.message == 0 &&
               parsed.total == 1 && ValidInfoQueue(record, nullptr);
    };
    auto messageTextAccepted = [&](const std::string& description) {
        const std::string record = V2Record(4, 1, description + '\0');
        InfoQueueCounts parsed;
        return ParseInfoQueue(record, parsed) && parsed.message == 1 &&
               parsed.warning == 0 && parsed.error == 0 &&
               parsed.corruption == 0 && parsed.info == 0 &&
               parsed.total == 1 && ValidInfoQueue(record, nullptr);
    };
    auto negativeSeverityRejected = [&](uint64_t severity,
                                        const std::string& description) {
        const std::string record = V2Record(static_cast<int>(severity), 1,
                                            description + '\0');
        InfoQueueCounts parsed;
        if (!ParseInfoQueue(record, parsed) || parsed.total != 1) return false;
        if (severity == kInfoQueueSeverityWarning && parsed.warning != 1)
            return false;
        if (severity == kInfoQueueSeverityError && parsed.error != 1)
            return false;
        if (severity == kInfoQueueSeverityCorruption &&
            parsed.corruption != 1)
            return false;
        return !ValidInfoQueue(record, nullptr);
    };
    check(infoTextAccepted("No ERROR occurred"),
          "severity-info-no-error-text");
    check(infoTextAccepted("WARNING count is zero"),
          "severity-info-warning-text");
    check(infoTextAccepted("CORRUPTION was not detected"),
          "severity-info-corruption-text");
    check(infoTextAccepted("No ERROR or WARNING occurred"),
          "severity-info-no-error-or-warning-text");
    check(infoTextAccepted("ERROR"), "severity-info-bare-error-text");
    check(infoTextAccepted("severity=1"), "severity-info-severity-key-text");
    check(infoTextAccepted("error warning corruption"),
          "severity-info-lowercase-text");
    check(infoTextAccepted("quoted \"ERROR\" and 'WARNING'"),
          "severity-info-quoted-text");
    check(infoTextAccepted("\xD0\x9F\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB5\xD1\x80\xD0\xBA\xD0\xB0 ERROR WARNING"),
          "severity-info-utf8-text");
    check(infoTextAccepted(std::string(65536, 'x') + " ERROR WARNING CORRUPTION"),
          "severity-info-large-text");
    check(messageTextAccepted("diagnostic message"),
          "severity-message-text");
    check(messageTextAccepted("MESSAGE with ERROR WARNING CORRUPTION"),
          "severity-message-key-like-text");
    check(negativeSeverityRejected(kInfoQueueSeverityWarning,
                                   "Normal operation"),
          "severity-warning-description-clean");
    check(negativeSeverityRejected(kInfoQueueSeverityWarning,
                                   "No warning here"),
          "severity-warning-description-negated");
    check(negativeSeverityRejected(kInfoQueueSeverityError, "All good"),
          "severity-error-description-clean");
    check(negativeSeverityRejected(kInfoQueueSeverityError,
                                   "ERROR\r\nmultiline"),
          "severity-error-crlf");
    check(negativeSeverityRejected(kInfoQueueSeverityCorruption,
                                   "arbitrary description"),
          "severity-corruption-description-clean");
    check(ParseInfoQueue("index=0 severity=3 id=7 description_length=17 description=No ERROR occurred\n",
                         severityCounts) && severityCounts.info == 1 &&
          ValidInfoQueue("index=0 severity=3 id=7 description_length=17 description=No ERROR occurred\n",
                         nullptr), "severity-v1-info-no-error-text");
    check(!ParseInfoQueue("format=v2 index=0 severity=5 id=1 description_length=1 description_hex=00\n",
                          severityCounts), "severity-invalid-enum-high");
    check(!ParseInfoQueue("format=v2 index=0 severity=0x3 id=1 description_length=1 description_hex=00\n",
                          severityCounts), "severity-invalid-enum-hex");
    check(!ParseInfoQueue("format=v2 index=0 severity= id=1 description_length=1 description_hex=00\n",
                          severityCounts), "severity-invalid-enum-empty");
    check(!ParseInfoQueue("format=v2 index=0 severity=-1 id=1 description_length=1 description_hex=00\n",
                          severityCounts), "severity-invalid-enum-negative");
    check(!ParseInfoQueue("format=v2 index=0 id=1 description_length=1 description_hex=00\n",
                          severityCounts), "severity-missing");
    check(!ParseInfoQueue("format=v2 index=0 severity=3 id=1 description_length=1 description_hex=00 trailing\n",
                          severityCounts), "severity-trailing-field");

    // F6: independent camera/readback audits are strict and sequential.
    std::vector<CameraAuditRecord> cameraAudit;
    check(ParseCameraAudit(
              "frame_index=1 session=1 epoch=1 sequence=1 camera_hash=0000000000000001 fence=2 status=VISIBLE_DRAW_VERIFIED\n",
              cameraAudit) && cameraAudit.size() == 1,
          "camera-audit-valid");
    check(!ParseCameraAudit(
              "frame_index=2 session=1 epoch=1 sequence=1 camera_hash=0000000000000001 fence=3 status=VISIBLE_DRAW_VERIFIED\n",
              cameraAudit), "camera-audit-gap-rejected");
    std::vector<ReadbackAuditRecord> readbackAudit;
    check(ParseReadbackAudit(
              "frame_index=1 session=1 epoch=1 sequence=1 viewport=1280x720 non_clear_pixels=1 bbox=0,0..0,0 depth_min=0 depth_max=1 cpu_confidence=GUARANTEED_INTERIOR_SAMPLE status=VISIBLE_DRAW_VERIFIED\n",
              readbackAudit) && readbackAudit.size() == 1,
          "readback-audit-valid");
    check(!ParseReadbackAudit(
              "frame_index=1 session=1 epoch=1 sequence=1 viewport=1280x720 non_clear_pixels=1 bbox=0,0..0,0 depth_min=0 depth_max=1 cpu_confidence=GUARANTEED_INTERIOR_SAMPLE\n",
              readbackAudit), "readback-audit-missing-status-rejected");

    // F1: the sampled set is reconstructed from mode/policy, not counters.
    const std::string offline3 = std::string(kCsvHeader) + "\n" +
        Line(1,true) + Line(2,true) + Line(3,true);
    CsvEvidence csv3;
    std::set<uint64_t> expected3;
    check(CsvValid(offline3,"rid",3,csv3) &&
          ExpectedSamples(csv3,"offline",1,expected3) && expected3.size()==3,
          "offline-all-samples-required");
    const std::string offline3Reduced = std::string(kCsvHeader) + "\n" +
        Line(1,true) + Line(2,true) + Line(3,false);
    CsvEvidence csv3r;
    check(CsvValid(offline3Reduced,"rid",3,csv3r) &&
          !ExpectedSamples(csv3r,"offline",1,expected3),
          "offline-reduced-set-rejected");
    const std::string live1 = std::string(kCsvHeader) + "\n" +
        Line(1,true,"live") + Line(2,true,"live");
    CsvEvidence csvLive1;
    check(CsvValid(live1,"rid",2,csvLive1) &&
          ExpectedSamples(csvLive1,"live",1,expected3) && expected3.size()==2,
          "live-sample-every-1");
    const std::string liveLast = std::string(kCsvHeader) + "\n" +
        Line(1,true,"live") + Line(2,false,"live") + Line(3,true,"last");
    CsvEvidence csvLiveLast;
    check(CsvValid(liveLast,"rid",3,csvLiveLast) &&
          ExpectedSamples(csvLiveLast,"live",10,expected3) && expected3.size()==2,
          "live-last-sample-every-10");
    check(!ExpectedSamples(csv3,"live",1,expected3), "offline-kind-not-live");
    const std::string falseLast = std::string(kCsvHeader) + "\n" +
        Line(1,true) + Line(2,true) + Line(3,true,"last");
    CsvEvidence csvFalseLast;
    check(!CsvValid(falseLast,"rid",3,csvFalseLast) ||
          !ExpectedSamples(csvFalseLast,"offline",1,expected3),
          "false-last-rejected");

    Fields removal = v;
    removal["completion_confirmed"] = "1";
    removal["device_removal_reason"] = "0x887A0005";
    check(!ValidValidation(removal), "validation-removal-rejected");

    std::cout << "evidence_cpu_checks=" << checks << " failures=0\n";
}
