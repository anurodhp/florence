// SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
// Runs on the Pi: ICU 74 (scripts/build_icu.sh), data in libicudata.74, what WebKit needs from it.
#include <cstdio>
#include <memory>
#include <string>
#include <unicode/brkiter.h>
#include <unicode/coll.h>
#include <unicode/numfmt.h>
#include <unicode/ucnv.h>
#include <unicode/uidna.h>
#include <unicode/unistr.h>
#include <unicode/uloc.h>
static int fails;
#define CHECK(c) do { bool ok_ = (c); std::printf("%s %s\n", ok_ ? "PASS" : "FAIL", #c); std::fflush(stdout); if (!ok_) fails++; } while (0)
int main() {
    UErrorCode e = U_ZERO_ERROR;
    icu::UnicodeString u = icu::UnicodeString::fromUTF8("stra\xC3\x9F" "e"); u.toUpper(icu::Locale("de")); std::string out; u.toUTF8String(out);
    CHECK(out == "STRASSE");
    std::unique_ptr<icu::Collator> col(icu::Collator::createInstance(icu::Locale("sv"), e)); CHECK(U_SUCCESS(e));
    CHECK(col->compare(icu::UnicodeString::fromUTF8("z"), icu::UnicodeString::fromUTF8("\xC3\xA4")) < 0);   // sv: z < a-umlaut
    std::unique_ptr<icu::BreakIterator> bi(icu::BreakIterator::createWordInstance(icu::Locale("en"), e)); CHECK(U_SUCCESS(e));
    icu::UnicodeString t = icu::UnicodeString::fromUTF8("hello big world"); bi->setText(t); int n = 0; while (bi->next() != icu::BreakIterator::DONE) n++; CHECK(n == 5);
    std::unique_ptr<icu::NumberFormat> nf(icu::NumberFormat::createInstance(icu::Locale("de"), e)); icu::UnicodeString f; nf->format(1234567.5, f);
    out.clear(); f.toUTF8String(out); CHECK(out == "1.234.567,5");
    UConverter *cv = ucnv_open("shift_jis", &e); CHECK(U_SUCCESS(e)); if (cv) ucnv_close(cv);
    UIDNA *idna = uidna_openUTS46(UIDNA_DEFAULT, &e); char dst[64]; UIDNAInfo info = UIDNA_INFO_INITIALIZER;
    int32_t len = uidna_nameToASCII_UTF8(idna, "b\xC3\xBC" "cher.example", -1, dst, sizeof dst, &info, &e); CHECK(U_SUCCESS(e) && std::string(dst, len) == "xn--bcher-kva.example"); uidna_close(idna);
    std::printf(fails ? "RESULT FAIL %d\n" : "RESULT OK\n", fails); return fails != 0;
}
