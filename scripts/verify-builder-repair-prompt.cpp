#include "../src/ui/shader_repair_prompt.hpp"
#include <cassert>
#include <iostream>
int main() {
    const QString code = QStringLiteral("float4 hs_shade_pixel(VertData v_in) : TARGET { return float4(1,0,0,1); }");
    const QString error = QStringLiteral("file.effect (8, 3): unexpected token");
    const QString prompt = makeShaderRepairPrompt(code, error);
    assert(prompt.contains(code));
    assert(prompt.contains(error));
    assert(prompt.contains("native hypershader"));
    assert(prompt.contains("complete corrected .effect"));
    assert(!prompt.contains("```"));
    assert(makeShaderRepairPrompt(QString(), error).isEmpty());
    assert(makeShaderRepairPrompt(code, QString()).isEmpty());
    assert(makeShaderRepairPrompt(QString(30000, QChar('x')), error).isEmpty());
    const QString truncated = makeShaderRepairPrompt(code, QString(7000, QChar('z')));
    assert(truncated.size() < 6700 && truncated.contains("truncated"));
    std::cout << "BUILDER_REPAIR_PROMPT_PASS=1\n";
}
