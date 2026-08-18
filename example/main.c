/*
    yksl サンプル
        Programed by あるる（きのもと 結衣） @arlez80
*/

#include "raylib.h"

#define YKSL_RAYLIB_SUPPORT
#define YKSL_IMPLEMENTATION

#include "yksl.h"

#if defined(PLATFORM_DESKTOP)
    #define GLSL_VERSION    330
#else
    #define GLSL_VERSION    100
#endif

static const char *ykslSource =
    "shader_type canvas_item;\n"
    "\n"
    "uniform float amount;\n"
    "\n"
    "void vertex()\n"
    "{\n"
    "    FRAG_UV = UV;\n"
    "    FRAG_COLOR = COLOR;\n"
    "    POSITION = MVP_MATRIX*vec4(VERTEX, 1.0);\n"
    "}\n"
    "\n"
    "void fragment()\n"
    "{\n"
    "    vec2 uv = UV;\n"
    "    uv.x += sin(uv.y*20.0 + TIME*3.0)*amount;\n"
    "    vec4 texel = texture(TEXTURE, uv);\n"
    "    COLOR = texel*MODULATE*FRAG_COLOR;\n"
    "}\n";

int main(void)
{
    InitWindow(800, 450, "yksl - Godot like shader for raylib");

    Texture2D texture = LoadTexture("test.png");

    // トランスパイル設定
    ykslOptions options = ykslGetDefaultOptions();
    options.glslVersion = GLSL_VERSION;
    options.highPrecisionCoords = true;

    // トランスパイルする
    char *vsCode = NULL;
    char *fsCode = NULL;
    if (ykslShaderTranspile(ykslSource, &options, &vsCode, &fsCode))
    {
        TraceLog(LOG_INFO, "--- VERTEX ---\n%s", vsCode);
        TraceLog(LOG_INFO, "--- FRAGMENT ---\n%s", fsCode);
    }
    else TraceLog(LOG_ERROR, "YKSL: %s", ykslGetTranspileError());

    ykslUnloadShaderCode(vsCode);
    ykslUnloadShaderCode(fsCode);

    // ---------------------------------------------------------------------

    Shader shader = ykslLoadShaderFromMemory(ykslSource, &options);

    int timeLoc = GetShaderLocation(shader, "time");
    int amountLoc = GetShaderLocation(shader, "amount");

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        float time = (float)GetTime();
        float amount = 0.02f;
        SetShaderValue(shader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, amountLoc, &amount, SHADER_UNIFORM_FLOAT);

        BeginDrawing();
            ClearBackground(RAYWHITE);
            BeginShaderMode(shader);
                DrawTexture(texture, 400 - texture.width/2, 225 - texture.height/2, WHITE);
            EndShaderMode();
            DrawText(TextFormat("GLSL %i", GLSL_VERSION), 10, 10, 20, DARKGRAY);
        EndDrawing();
    }

    UnloadShader(shader);
    UnloadTexture(texture);
    CloseWindow();

    return 0;
}
