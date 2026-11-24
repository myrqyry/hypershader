// Regression for distinguishing shader declarations from prose comments.
#include "../src/ui/shader_code_classification.hpp"
#include <QRegularExpression>
#include <iostream>
int main()
{
    const QString code = "// technique Draw is supplied by the wrapper\n"
                         "/* #include hypershader_standard.glsl "
                         "HYPERSHADER_MAINIMAGE_WRAPPER */\n"
                         "void mainImage(out vec4 color, in vec2 uv) { color = vec4(1.0); }";
    const QString bare = stripShaderComments(code);
    const bool main = QRegularExpression(R"(\bmainImage\s*\()").match(bare).hasMatch();
    const bool technique = QRegularExpression(R"(\btechnique\s+)").match(bare).hasMatch();
    if (!main || technique || bare.contains("HYPERSHADER_MAINIMAGE_WRAPPER") ||
        bare.contains("hypershader_standard.glsl")) return 1;
    const QString strings = "const char* url = \"https://example.org/\"; // technique Draw\n"
                            "technique Draw { pass { } }";
    const QString clean = stripShaderComments(strings);
    if (!clean.contains("https://example.org/") ||
        !QRegularExpression(R"(\btechnique\s+)").match(clean).hasMatch()) return 2;
    std::cout << "PASS: comments cannot disguise generated shader shape\n";
    return 0;
}
