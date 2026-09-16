#include "presentation/chart_export.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Require(bool condition, const char* message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

}

int main() {
    using squarestar::presentation::EscapeSpreadsheetCsvField;
    using squarestar::presentation::NormalizeSpreadsheetTextField;

    Require(EscapeSpreadsheetCsvField("=SUM(A1:A2)") == "'=SUM(A1:A2)",
            "CSV text beginning with '=' is neutralized before spreadsheet import");
    Require(EscapeSpreadsheetCsvField("  +1") == "'  +1",
            "CSV text with leading whitespace cannot hide a formula prefix");
    Require(EscapeSpreadsheetCsvField("Acme, Inc.") == "\"Acme, Inc.\"",
            "CSV quoting still handles commas after hardening");
    Require(NormalizeSpreadsheetTextField("@cmd\trow\r\nnext") ==
                "'@cmd row  next",
            "tab-delimited text removes control characters and neutralizes formulas");
    Require(NormalizeSpreadsheetTextField("Normal Company") == "Normal Company",
            "ordinary spreadsheet text is unchanged");

    return EXIT_SUCCESS;
}
