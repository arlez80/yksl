/*
    yksl テストコード
        Programed by あるる（きのもと 結衣） @arlez80
*/

#define YKSL_IMPLEMENTATION
#include "yksl.h"
#include <stdio.h>

static const char *shaderCode =
"shader_type canvas_item;\n"
"\n"
"uniform vec4 tint;\n"
"uniform float strength;\n"
"uniform samplerCube envMap;\n"
"\n"
"varying vec3 worldPos;\n"
"\n"
"const float PI = 3.14159;\n"
"\n"
"float wave(float x)\n"
"{\n"
"    return sin(x*PI*2.0)*0.5 + 0.5;\n"
"}\n"
"\n"
"void vertex()\n"
"{\n"
"    worldPos = (MODEL_MATRIX*vec4(VERTEX, 1.0)).xyz;\n"
"    FRAG_UV = UV;\n"
"    FRAG_COLOR = COLOR;\n"
"    POSITION = MVP_MATRIX*vec4(VERTEX, 1.0);\n"
"}\n"
"\n"
"void fragment()\n"
"{\n"
"    vec2 uv = UV;\n"
"    uv.x += wave(uv.y + TIME)*strength;\n"
"    vec4 base = texture(TEXTURE, uv)*MODULATE*FRAG_COLOR;\n"
"    base.rgb += texture(envMap, worldPos).rgb*0.1;\n"
"    if (base.a < 0.01) discard;\n"
"    COLOR = base*tint;\n"
"}\n";

int main(void)
{
    int versions[] = { 100, 120, 300, 330 };

    for (int i = 0; i < 4; i++)
    {
        char *vs = NULL;
        char *fs = NULL;

        ykslOptions options = ykslGetDefaultOptions();
        options.glslVersion = versions[i];
        options.highPrecisionCoords = true;

        if (!ykslShaderTranspile(shaderCode, &options, &vs, &fs))
        {
            printf("ERROR: %s\n", ykslGetTranspileError());
            continue;
        }

        printf("=========== GLSL %d : VERTEX ===========\n%s", versions[i], vs);
        printf("=========== GLSL %d : FRAGMENT =========\n%s", versions[i], fs);

        ykslUnloadShaderCode(vs);
        ykslUnloadShaderCode(fs);
    }

    // フラグメントだけ欲しい場合は頂点側に NULL を渡す
    {
        char *fs = NULL;
        if (ykslShaderTranspile("void fragment() { COLOR = vec4(UV, 0.0, 1.0); }", NULL, NULL, &fs))   // options = NULL -> 既定 (330)
        {
            printf("=========== fragment only ===========\n%s", fs);
            ykslUnloadShaderCode(fs);
        }
    }

    // 片方だけ定義されたソースで両方要求
    {
        char *vs = (char *)1;
        char *fs = (char *)1;
        bool ok = ykslShaderTranspile("void fragment() { COLOR = vec4(1.0); }", NULL, &vs, &fs);
        printf("== error test: ok=%d vs=%p fs=%p : %s\n", (int)ok, (void *)vs, (void *)fs, ykslGetTranspileError());
    }

    {
        char *vs = NULL;
        ykslOptions options = ykslGetDefaultOptions();
        options.glslVersion = 150;
        bool ok = ykslShaderTranspile("void vertex() {}", &options, &vs, NULL);
        printf("== error test: ok=%d : %s\n", (int)ok, ykslGetTranspileError());
    }

    {
        bool ok = ykslShaderTranspile("void vertex() {}", NULL, NULL, NULL);
        printf("== error test: ok=%d : %s\n", (int)ok, ykslGetTranspileError());
    }

    // fragmentPrecision = HIGH (ES2 で shader 全体を highp に寄せる)
    {
        char *fs = NULL;
        ykslOptions options = ykslGetDefaultOptions();
        options.glslVersion = YKSL_GLSL_100;
        options.fragmentPrecision = YKSL_PRECISION_HIGH;

        if (ykslShaderTranspile("uniform highp float scale;\n"
                                "void fragment() { COLOR = texture(TEXTURE, UV*scale); }", &options, NULL, &fs))
        {
            printf("=========== ES2 fragmentPrecision = HIGH ===========\n%s", fs);
            ykslUnloadShaderCode(fs);
        }
    }

    return 0;
}
