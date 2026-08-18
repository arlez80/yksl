/*
    Godot風シェーダ言語 GLSLトランスパイラ
        Programed by あるる（きのもと 結衣） @arlez80

    MIT License

    Copyright (c) 2026 きのもと 結衣

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#ifndef YKSL_H
#define YKSL_H

#ifndef YKSLAPI
    #define YKSLAPI extern
#endif

#if !defined(__cplusplus) && !defined(bool) && !defined(RL_BOOL_TYPE)
    #include <stdbool.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

// ----------------------------------------------------------------------------
// API
// ----------------------------------------------------------------------------

// 出力対象の GLSL バージョン
typedef enum {
    YKSL_GLSL_100 = 100,    // OpenGL ES 2.0 / WebGL 1 -> "#version 100"
    YKSL_GLSL_120 = 120,    // OpenGL 2.1              -> "#version 120"
    YKSL_GLSL_300 = 300,    // OpenGL ES 3.0 / WebGL 2 -> "#version 300 es"
    YKSL_GLSL_330 = 330     // OpenGL 3.3              -> "#version 330"
} ykslGlslVersion;

// フラグメントシェーダの既定 float 精度
// OpenGL ES (100 / 300) でのみ意味を持つ。デスクトップ向けでは無視される
typedef enum {
    YKSL_PRECISION_DEFAULT = 0, // mediump
    YKSL_PRECISION_LOW,         // lowp
    YKSL_PRECISION_MEDIUM,      // mediump
    YKSL_PRECISION_HIGH         // highp
} ykslPrecision;

// トランスパイル設定
typedef struct ykslOptions {
    int  glslVersion;           // GLSLバージョン
    int  fragmentPrecision;     // フラグメントシェーダでのfloat精度。ES以外では無視
    bool highPrecisionCoords;   // 座標計算を高精度に（ESのみ）
} ykslOptions;

// 既定値で初期化された設定を得る
YKSLAPI ykslOptions ykslGetDefaultOptions(void);

// ykSL のソースを GLSL へトランスパイルする
YKSLAPI bool ykslShaderTranspile(const char *code, const ykslOptions *options, char **vsCode, char **fsCode);

// トランスパイル結果を解放する
YKSLAPI void ykslUnloadShaderCode(char *code);

// 直近のエラーメッセージを得る
YKSLAPI const char *ykslGetTranspileError(void);

#if defined(YKSL_RAYLIB_SUPPORT)
// ykSL のソースから raylib の Shader を直接生成する
YKSLAPI Shader ykslLoadShaderFromMemory(const char *code, const ykslOptions *options);
#endif

#if defined(__cplusplus)
}
#endif

#endif /* YKSL_H */

// ----------------------------------------------------------------------------
// 実装
// ----------------------------------------------------------------------------

#if defined(YKSL_IMPLEMENTATION)

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

#ifndef YKSL_MALLOC
    #define YKSL_MALLOC(sz)       malloc(sz)
#endif
#ifndef YKSL_REALLOC
    #define YKSL_REALLOC(p, sz)   realloc(p, sz)
#endif
#ifndef YKSL_FREE
    #define YKSL_FREE(p)          free(p)
#endif

#define YKSL_MAX_SYMBOLS      256   // 追跡できる uniform / varying の最大数
#define YKSL_MAX_BUILTINS      64   // 組み込み変数テーブルの上限
#define YKSL_STAGE_VERTEX       1   // 頂点シェーダ
#define YKSL_STAGE_FRAGMENT     2   // フラグメントシェーダ
#define YKSL_STAGE_BOTH         3   // 両シェーダー

//----------------------------------------------------------------------------------
// エラー処理
//----------------------------------------------------------------------------------
static char ykslErrorMessage[512] = { 0 };  // 直近のエラーメッセージ
static int ykslErrorMissingEntry = 0;       // 直近のエラーが「エントリ関数が無い」だったか

// エラーメッセージを設定する
static void ykslSetError(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(ykslErrorMessage, sizeof(ykslErrorMessage), format, args);
    va_end(args);
}

const char *ykslGetTranspileError(void) { return ykslErrorMessage; }

void ykslUnloadShaderCode(char *code) { if (code != NULL) YKSL_FREE(code); }

//----------------------------------------------------------------------------------
// 動的文字列
//----------------------------------------------------------------------------------

// 伸長する文字列バッファ
typedef struct ykslStr {
    char *data;         // NUL 終端された文字列
    int length;         // 現在の長さ
    int capacity;       // 確保済みのバイト数
} ykslStr;

// 文字列バッファを初期化する
static void ykslStrInit(ykslStr *s)
{
    s->capacity = 1024;
    s->length = 0;
    s->data = (char *)YKSL_MALLOC(s->capacity);
    if (s->data != NULL) s->data[0] = '\0';
}

// 長さを指定して文字列を追加する
static void ykslStrAppendN(ykslStr *s, const char *text, int n)
{
    if ((s->data == NULL) || (n <= 0)) return;

    if ((s->length + n + 1) > s->capacity)
    {
        int newCapacity = s->capacity;
        while ((s->length + n + 1) > newCapacity) newCapacity *= 2;

        char *newData = (char *)YKSL_REALLOC(s->data, newCapacity);
        if (newData == NULL) { YKSL_FREE(s->data); s->data = NULL; return; }

        s->data = newData;
        s->capacity = newCapacity;
    }

    memcpy(s->data + s->length, text, n);
    s->length += n;
    s->data[s->length] = '\0';
}

// NUL 終端文字列を追加する
static void ykslStrAppend(ykslStr *s, const char *text)
{
    if (text != NULL) ykslStrAppendN(s, text, (int)strlen(text));
}

// 文字列バッファを解放する
static void ykslStrFree(ykslStr *s)
{
    if (s->data != NULL) YKSL_FREE(s->data);
    s->data = NULL;
    s->length = 0;
    s->capacity = 0;
}

// 長さを指定して文字列を複製する
static char *ykslStrDupN(const char *text, int n)
{
    char *result = (char *)YKSL_MALLOC(n + 1);
    if (result == NULL) return NULL;
    memcpy(result, text, n);
    result[n] = '\0';
    return result;
}

//----------------------------------------------------------------------------------
// トークナイザ
//----------------------------------------------------------------------------------

// トークンの種類
typedef enum {
    YKSL_TOKEN_EOF = 0,     // 終端
    YKSL_TOKEN_IDENT,       // 識別子・キーワード
    YKSL_TOKEN_NUMBER,      // 数値リテラル
    YKSL_TOKEN_PUNCT,       // 記号・演算子
    YKSL_TOKEN_PREPROC      // プリプロセッサ行（1行まるごと）
} ykslTokenType;

// トークン情報
typedef struct ykslToken {
    ykslTokenType type;     // 種類
    char *text;             // 文字列（確保済み）
    int line;               // ソース上の行番号
    int newline;            // このトークンの前に改行があったか
} ykslToken;

// トークンの可変長配列
typedef struct ykslTokenList {
    ykslToken *items;       // トークン配列
    int count;              // 個数
    int capacity;           // 確保済み要素数
} ykslTokenList;

// トークンを追加する
static void ykslTokenListPush(ykslTokenList *list, ykslTokenType type, const char *text, int length, int line, int newline)
{
    if (list->count >= list->capacity)
    {
        int newCapacity = (list->capacity == 0)? 256 : list->capacity*2;
        ykslToken *items = (ykslToken *)YKSL_REALLOC(list->items, sizeof(ykslToken)*newCapacity);
        if (items == NULL) return;
        list->items = items;
        list->capacity = newCapacity;
    }

    if (list->items == NULL) return;

    list->items[list->count].type = type;
    list->items[list->count].text = ykslStrDupN(text, length);
    list->items[list->count].line = line;
    list->items[list->count].newline = newline;
    list->count++;
}

// トークン配列を解放する
static void ykslTokenListFree(ykslTokenList *list)
{
    for (int i = 0; i < list->count; i++) YKSL_FREE(list->items[i].text);
    if (list->items != NULL) YKSL_FREE(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

// 識別子の先頭に使える文字か
static int ykslIsIdentStart(int c) { return (isalpha(c) || (c == '_')); }
// 識別子に使える文字か
static int ykslIsIdentChar(int c)  { return (isalnum(c) || (c == '_')); }

static const char *ykslOperators3[] = { "<<=", ">>=", NULL };   // 3文字演算子
static const char *ykslOperators2[] = {                         // 2文字演算子
    "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "^^",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "++", "--", NULL
};

// ソースをトークンへ分解する
static int ykslTokenize(const char *code, ykslTokenList *list)
{
    const char *p = code;
    int line = 1;
    int newline = 1;

    while (*p != '\0')
    {
        // 空白・改行
        if (*p == '\n') { line++; newline = 1; p++; continue; }
        if (isspace((unsigned char)*p)) { p++; continue; }

        // コメント
        if ((p[0] == '/') && (p[1] == '/'))
        {
            while ((*p != '\0') && (*p != '\n')) p++;
            continue;
        }
        if ((p[0] == '/') && (p[1] == '*'))
        {
            p += 2;
            while ((*p != '\0') && !((p[0] == '*') && (p[1] == '/')))
            {
                if (*p == '\n') { line++; newline = 1; }
                p++;
            }
            if (*p != '\0') p += 2;
            continue;
        }

        // プリプロセッサ行
        if (*p == '#')
        {
            const char *start = p;
            while (*p != '\0')
            {
                if ((p[0] == '\\') && (p[1] == '\n')) { line++; p += 2; continue; }
                if (*p == '\n') break;
                p++;
            }
            ykslTokenListPush(list, YKSL_TOKEN_PREPROC, start, (int)(p - start), line, 1);
            newline = 1;
            continue;
        }

        // 識別子・キーワード
        if (ykslIsIdentStart((unsigned char)*p))
        {
            const char *start = p;
            while (ykslIsIdentChar((unsigned char)*p)) p++;
            ykslTokenListPush(list, YKSL_TOKEN_IDENT, start, (int)(p - start), line, newline);
            newline = 0;
            continue;
        }

        // 数値
        if (isdigit((unsigned char)*p) || ((*p == '.') && isdigit((unsigned char)p[1])))
        {
            const char *start = p;
            while (isdigit((unsigned char)*p)) p++;
            if (*p == '.') { p++; while (isdigit((unsigned char)*p)) p++; }
            if ((*p == 'e') || (*p == 'E'))
            {
                const char *save = p;
                p++;
                if ((*p == '+') || (*p == '-')) p++;
                if (isdigit((unsigned char)*p)) { while (isdigit((unsigned char)*p)) p++; }
                else p = save;
            }
            while ((*p == 'f') || (*p == 'F') || (*p == 'u') || (*p == 'U')) p++;
            ykslTokenListPush(list, YKSL_TOKEN_NUMBER, start, (int)(p - start), line, newline);
            newline = 0;
            continue;
        }

        // 記号
        {
            int matched = 0;
            for (int i = 0; ykslOperators3[i] != NULL; i++)
            {
                if (strncmp(p, ykslOperators3[i], 3) == 0)
                {
                    ykslTokenListPush(list, YKSL_TOKEN_PUNCT, p, 3, line, newline);
                    p += 3; matched = 1; break;
                }
            }
            if (!matched)
            {
                for (int i = 0; ykslOperators2[i] != NULL; i++)
                {
                    if (strncmp(p, ykslOperators2[i], 2) == 0)
                    {
                        ykslTokenListPush(list, YKSL_TOKEN_PUNCT, p, 2, line, newline);
                        p += 2; matched = 1; break;
                    }
                }
            }
            if (!matched)
            {
                ykslTokenListPush(list, YKSL_TOKEN_PUNCT, p, 1, line, newline);
                p += 1;
            }
            newline = 0;
        }
    }

    return 1;
}

//----------------------------------------------------------------------------------
// 組み込み変数テーブル (ykSL 名 -> raylib の GLSL 名)
//----------------------------------------------------------------------------------

// 組み込み変数の種別
typedef enum {
    YKSL_BI_ATTRIBUTE = 0,  // 頂点属性 (attribute / in)
    YKSL_BI_VARYING,        // 頂点 -> フラグメント (varying / in,out)
    YKSL_BI_UNIFORM,        // uniform
    YKSL_BI_SPECIAL,        // gl_* に直結するため宣言不要
    YKSL_BI_OUTCOLOR        // フラグメント出力色 (gl_FragColor / finalColor)
} ykslBuiltinKind;

// 組み込み変数1件の定義
typedef struct ykslBuiltinInfo {
    const char *name;       // ykSL 側の名前
    const char *glName;     // GLSL 側の名前
    const char *type;       // 宣言に使う型
    ykslBuiltinKind kind;   // 種別
    int stages;             // 有効なステージ（YKSL_STAGE_* のビットマスク）
} ykslBuiltinInfo;

// 組み込み変数テーブル（ykSL 名 -> raylib の GLSL 名）
static const ykslBuiltinInfo ykslBuiltins[] = {
    // 頂点属性
    //   raylib の既定 attribute 名
    { "VERTEX",            "vertexPosition",  "vec3",      YKSL_BI_ATTRIBUTE, YKSL_STAGE_VERTEX },
    { "UV",                "vertexTexCoord",  "vec2",      YKSL_BI_ATTRIBUTE, YKSL_STAGE_VERTEX },
    { "UV2",               "vertexTexCoord2", "vec2",      YKSL_BI_ATTRIBUTE, YKSL_STAGE_VERTEX },
    { "NORMAL",            "vertexNormal",    "vec3",      YKSL_BI_ATTRIBUTE, YKSL_STAGE_VERTEX },
    { "TANGENT",           "vertexTangent",   "vec4",      YKSL_BI_ATTRIBUTE, YKSL_STAGE_VERTEX },
    { "COLOR",             "vertexColor",     "vec4",      YKSL_BI_ATTRIBUTE, YKSL_STAGE_VERTEX },

    // varying
    //   raylib の既定 varying 名
    { "UV",                "fragTexCoord",    "vec2",      YKSL_BI_VARYING,   YKSL_STAGE_FRAGMENT },
    { "FRAG_UV",           "fragTexCoord",    "vec2",      YKSL_BI_VARYING,   YKSL_STAGE_BOTH },
    { "FRAG_COLOR",        "fragColor",       "vec4",      YKSL_BI_VARYING,   YKSL_STAGE_BOTH },
    { "FRAG_NORMAL",       "fragNormal",      "vec3",      YKSL_BI_VARYING,   YKSL_STAGE_BOTH },
    { "FRAG_POSITION",     "fragPosition",    "vec3",      YKSL_BI_VARYING,   YKSL_STAGE_BOTH },

    // uniform
    //   raylib が自動で設定するもの
    { "TEXTURE",           "texture0",        "sampler2D", YKSL_BI_UNIFORM,   YKSL_STAGE_BOTH },
    { "TEXTURE1",          "texture1",        "sampler2D", YKSL_BI_UNIFORM,   YKSL_STAGE_BOTH },
    { "TEXTURE2",          "texture2",        "sampler2D", YKSL_BI_UNIFORM,   YKSL_STAGE_BOTH },
    { "MODULATE",          "colDiffuse",      "vec4",      YKSL_BI_UNIFORM,   YKSL_STAGE_BOTH },
    { "COLOR_DIFFUSE",     "colDiffuse",      "vec4",      YKSL_BI_UNIFORM,   YKSL_STAGE_BOTH },
    { "MVP_MATRIX",        "mvp",             "mat4",      YKSL_BI_UNIFORM,   YKSL_STAGE_BOTH },
    { "MODEL_MATRIX",      "matModel",        "mat4",      YKSL_BI_UNIFORM,   YKSL_STAGE_BOTH },
    { "VIEW_MATRIX",       "matView",         "mat4",      YKSL_BI_UNIFORM,   YKSL_STAGE_BOTH },
    { "PROJECTION_MATRIX", "matProjection",   "mat4",      YKSL_BI_UNIFORM,   YKSL_STAGE_BOTH },
    { "NORMAL_MATRIX",     "matNormal",       "mat4",      YKSL_BI_UNIFORM,   YKSL_STAGE_BOTH },
    { "TIME",              "time",            "float",     YKSL_BI_UNIFORM,   YKSL_STAGE_BOTH },

    // gl_* 直結
    { "POSITION",          "gl_Position",     NULL,        YKSL_BI_SPECIAL,   YKSL_STAGE_VERTEX },
    { "POINT_SIZE",        "gl_PointSize",    NULL,        YKSL_BI_SPECIAL,   YKSL_STAGE_VERTEX },
    { "FRAGCOORD",         "gl_FragCoord",    NULL,        YKSL_BI_SPECIAL,   YKSL_STAGE_FRAGMENT },
    { "FRONT_FACING",      "gl_FrontFacing",  NULL,        YKSL_BI_SPECIAL,   YKSL_STAGE_FRAGMENT },
    { "POINT_COORD",       "gl_PointCoord",   NULL,        YKSL_BI_SPECIAL,   YKSL_STAGE_FRAGMENT },

    // 出力色
    { "COLOR",             NULL,              "vec4",      YKSL_BI_OUTCOLOR,  YKSL_STAGE_FRAGMENT },

    { NULL, NULL, NULL, YKSL_BI_SPECIAL, 0 }
};

//----------------------------------------------------------------------------------
// トランスパイル用コンテキスト
//----------------------------------------------------------------------------------

// ソース中で宣言された変数（サンプラ型の判定に使う）
typedef struct ykslSymbol {
    char *name;             // 変数名
    char *type;             // 型名
} ykslSymbol;

// 1回のトランスパイル中に持ち回す状態
typedef struct ykslContext {
    ykslOptions options;                        // 解決済みの設定
    int version;                                // 出力する GLSL バージョン
    int stage;                                  // 処理中のステージ (YKSL_STAGE_*)
    int highpMacro;                             // YKSL_HIGHP マクロを出力したか
    int coordHighp;                             // 座標系の組み込み変数に YKSL_HIGHP を付けるか
    int used[YKSL_MAX_BUILTINS];                // 使用された組み込み変数のフラグ
    ykslSymbol symbols[YKSL_MAX_SYMBOLS];       // 宣言された変数
    int symbolCount;                            // symbols の個数
    int failed;                                 // エラーが発生したか
} ykslContext;

// OpenGL ES 向けのバージョンか
static int ykslIsES(int version)      { return ((version == 100) || (version == 300)); }

// in/out 構文を使うバージョンか
static int ykslIsModern(int version)  { return (version >= 300); }

ykslOptions ykslGetDefaultOptions(void)
{
    ykslOptions options;

    options.glslVersion = YKSL_GLSL_330;
    options.fragmentPrecision = YKSL_PRECISION_DEFAULT;
    options.highPrecisionCoords = false;

    return options;
}

// NULL や未設定の項目を既定値で埋める
static ykslOptions ykslResolveOptions(const ykslOptions *options)
{
    ykslOptions result = ykslGetDefaultOptions();

    if (options != NULL) result = *options;
    if (result.glslVersion == 0) result.glslVersion = YKSL_GLSL_330;

    return result;
}

// 座標計算に使われる組み込み変数か？
static int ykslIsCoordBuiltin(const char *glName)
{
    static const char *names[] = {
        "vertexPosition", "vertexTexCoord", "vertexTexCoord2",
        "fragTexCoord", "fragPosition",
        "mvp", "matModel", "matView", "matProjection", "matNormal",
        "time", NULL
    };

    for (int i = 0; names[i] != NULL; i++) if (strcmp(names[i], glName) == 0) return 1;
    return 0;
}

// 組み込み変数テーブルを検索する
static int ykslFindBuiltin(const char *name, int stage)
{
    for (int i = 0; ykslBuiltins[i].name != NULL; i++)
    {
        if ((ykslBuiltins[i].stages & stage) && (strcmp(ykslBuiltins[i].name, name) == 0)) return i;
    }
    return -1;
}

// フラグメント出力色の GLSL 名を得る
static const char *ykslOutColorName(ykslContext *ctx)
{
    return ykslIsModern(ctx->version)? "finalColor" : "gl_FragColor";
}

// 宣言された変数を記録する
static void ykslAddSymbol(ykslContext *ctx, const char *type, const char *name)
{
    if (ctx->symbolCount >= YKSL_MAX_SYMBOLS) return;
    ctx->symbols[ctx->symbolCount].type = ykslStrDupN(type, (int)strlen(type));
    ctx->symbols[ctx->symbolCount].name = ykslStrDupN(name, (int)strlen(name));
    ctx->symbolCount++;
}

// 記録済みの変数の型を得る
static const char *ykslFindSymbolType(ykslContext *ctx, const char *name)
{
    for (int i = 0; i < ctx->symbolCount; i++)
    {
        if (strcmp(ctx->symbols[i].name, name) == 0) return ctx->symbols[i].type;
    }
    return NULL;
}

// 記録済みの変数を解放する
static void ykslFreeSymbols(ykslContext *ctx)
{
    for (int i = 0; i < ctx->symbolCount; i++)
    {
        YKSL_FREE(ctx->symbols[i].name);
        YKSL_FREE(ctx->symbols[i].type);
    }
    ctx->symbolCount = 0;
}

//----------------------------------------------------------------------------------
// トークン出力 (整形付き)
//----------------------------------------------------------------------------------
// 整形しながらトークンを書き出す状態
typedef struct ykslEmitter {
    ykslStr *out;           // 出力先バッファ
    int depth;              // 現在のブレース深さ（インデント量）
    int atLineStart;        // 行頭にいるか
} ykslEmitter;

// 現在の深さぶんインデントを出力する
static void ykslEmitIndent(ykslEmitter *e)
{
    for (int i = 0; i < e->depth; i++) ykslStrAppend(e->out, "    ");
}

// GLSL のキーワードか（空白の入れ方の判定に使う）
static int ykslIsKeyword(const char *text)
{
    static const char *keywords[] = {
        "if", "else", "for", "while", "do", "return", "switch", "case",
        "discard", "in", "out", "inout", "const", "uniform", "varying",
        "attribute", "struct", "break", "continue", "lowp", "mediump", "highp", NULL
    };
    for (int i = 0; keywords[i] != NULL; i++) if (strcmp(keywords[i], text) == 0) return 1;
    return 0;
}

// 2つのトークンの間に空白が必要か判定する
static int ykslNeedSpace(const ykslToken *prev, const ykslToken *prevprev, const ykslToken *cur)
{
    if (prev == NULL) return 0;

    const char *p = prev->text;
    const char *c = cur->text;

    if (cur->type == YKSL_TOKEN_PUNCT)
    {
        // 後置インクリメント/デクリメント
        if (((strcmp(c, "++") == 0) || (strcmp(c, "--") == 0)) &&
            ((prev->type == YKSL_TOKEN_IDENT) || (strcmp(p, ")") == 0) || (strcmp(p, "]") == 0))) return 0;

        if ((strcmp(c, ",") == 0) || (strcmp(c, ";") == 0) || (strcmp(c, ")") == 0) ||
            (strcmp(c, "]") == 0) || (strcmp(c, ".") == 0) || (strcmp(c, "[") == 0)) return 0;

        if (strcmp(c, "(") == 0)
        {
            // 関数呼び出し / 宣言なら空白なし、キーワードの後なら空白あり
            if ((prev->type == YKSL_TOKEN_IDENT) && !ykslIsKeyword(p)) return 0;
            if ((strcmp(p, ")") == 0) || (strcmp(p, "]") == 0)) return 0;
            return 1;
        }
    }

    if (prev->type == YKSL_TOKEN_PUNCT)
    {
        if ((strcmp(p, "(") == 0) || (strcmp(p, "[") == 0) || (strcmp(p, ".") == 0) ||
            (strcmp(p, "!") == 0)) return 0;

        // 単項 + - ~ の判定
        if ((strcmp(p, "-") == 0) || (strcmp(p, "+") == 0) || (strcmp(p, "~") == 0))
        {
            if (prevprev == NULL) return 0;
            if ((prevprev->type == YKSL_TOKEN_PUNCT) &&
                (strcmp(prevprev->text, ")") != 0) && (strcmp(prevprev->text, "]") != 0)) return 0;
        }
    }

    return 1;
}

// ykSL の識別子を GLSL の識別子へ変換する
static const char *ykslMapIdentifier(ykslContext *ctx, const char *name)
{
    int index = ykslFindBuiltin(name, ctx->stage);
    if (index < 0) return name;

    ctx->used[index] = 1;

    if (ykslBuiltins[index].kind == YKSL_BI_OUTCOLOR) return ykslOutColorName(ctx);
    return ykslBuiltins[index].glName;
}

// texture() を旧 GLSL の関数名へ置き換える
static const char *ykslLegacyTextureName(ykslContext *ctx, const char *samplerName, const char *base)
{
    const char *type = NULL;

    int index = ykslFindBuiltin(samplerName, ctx->stage);
    if ((index >= 0) && (ykslBuiltins[index].type != NULL)) type = ykslBuiltins[index].type;
    else type = ykslFindSymbolType(ctx, samplerName);

    if (type != NULL)
    {
        if (strstr(type, "Cube") != NULL) return (strcmp(base, "textureLod") == 0)? "textureCubeLod" : "textureCube";
        if (strstr(type, "3D") != NULL)   return (strcmp(base, "textureLod") == 0)? "texture3DLod"   : "texture3D";
    }

    return (strcmp(base, "textureLod") == 0)? "texture2DLod" : "texture2D";
}

// トークン列を変換しながら出力する
static void ykslEmitRange(ykslEmitter *e, ykslContext *ctx, ykslTokenList *list, int start, int end)
{
    for (int i = start; i <= end; i++)
    {
        ykslToken *token = &list->items[i];
        ykslToken *prev = (i > start)? &list->items[i - 1] : NULL;
        ykslToken *prevprev = (i > start + 1)? &list->items[i - 2] : NULL;

        if ((token->type == YKSL_TOKEN_PUNCT) && (strcmp(token->text, "}") == 0) && (e->depth > 0)) e->depth--;

        if (token->type == YKSL_TOKEN_PREPROC)
        {
            if (strncmp(token->text, "#version", 8) == 0) continue;    // バージョンは自動生成
            if (!e->atLineStart) ykslStrAppend(e->out, "\n");
            ykslStrAppend(e->out, token->text);
            ykslStrAppend(e->out, "\n");
            e->atLineStart = 1;
            continue;
        }

        if (token->type == YKSL_TOKEN_IDENT)
        {
            // GLSL 1.20 は精度修飾子を持たないので取り除く
            if ((ctx->version == 120) &&
                ((strcmp(token->text, "highp") == 0) || (strcmp(token->text, "mediump") == 0) ||
                 (strcmp(token->text, "lowp") == 0))) continue;
        }

        if (e->atLineStart) ykslEmitIndent(e);
        else if ((token->newline) && (i > start)) { ykslStrAppend(e->out, "\n"); ykslEmitIndent(e); }
        else if (ykslNeedSpace(prev, prevprev, token)) ykslStrAppend(e->out, " ");

        e->atLineStart = 0;

        if (token->type == YKSL_TOKEN_IDENT)
        {
            // ユーザーが書いた highp も端末依存を避けるためマクロ経由にする
            if (ctx->highpMacro && (strcmp(token->text, "highp") == 0))
            {
                ykslStrAppend(e->out, "YKSL_HIGHP");
                continue;
            }

            // texture() / textureLod() の旧バージョン向け書き換え
            if (!ykslIsModern(ctx->version) &&
                ((strcmp(token->text, "texture") == 0) || (strcmp(token->text, "textureLod") == 0)) &&
                (i + 2 <= end) && (strcmp(list->items[i + 1].text, "(") == 0))
            {
                if ((strcmp(token->text, "textureLod") == 0) && (ctx->stage == YKSL_STAGE_FRAGMENT))
                {
                    ykslSetError("textureLod() は GLSL %d のフラグメントシェーダでは使用できません (行 %d)", ctx->version, token->line);
                    ctx->failed = 1;
                }

                const char *sampler = (list->items[i + 2].type == YKSL_TOKEN_IDENT)? list->items[i + 2].text : "";
                ykslStrAppend(e->out, ykslLegacyTextureName(ctx, sampler, token->text));
                continue;
            }

            if (strcmp(token->text, "texelFetch") == 0 && !ykslIsModern(ctx->version))
            {
                ykslSetError("texelFetch() は GLSL %d では使用できません (行 %d)", ctx->version, token->line);
                ctx->failed = 1;
            }

            ykslStrAppend(e->out, ykslMapIdentifier(ctx, token->text));
            continue;
        }

        ykslStrAppend(e->out, token->text);

        if ((token->type == YKSL_TOKEN_PUNCT) && (strcmp(token->text, "{") == 0)) e->depth++;
    }
}

//----------------------------------------------------------------------------------
// 補助: トークン検索
//----------------------------------------------------------------------------------

// トークンが指定した記号か
static int ykslIsPunct(ykslToken *token, const char *text)
{
    return ((token->type == YKSL_TOKEN_PUNCT) && (strcmp(token->text, text) == 0));
}

// 指定した記号を前方検索する
static int ykslFindPunct(ykslTokenList *list, int start, const char *text)
{
    for (int i = start; i < list->count; i++) if (ykslIsPunct(&list->items[i], text)) return i;
    return -1;
}

// 対応する閉じブレースを探す
static int ykslMatchBrace(ykslTokenList *list, int openIndex)
{
    int depth = 0;
    for (int i = openIndex; i < list->count; i++)
    {
        if (ykslIsPunct(&list->items[i], "{")) depth++;
        else if (ykslIsPunct(&list->items[i], "}"))
        {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

//----------------------------------------------------------------------------------
// 宣言ブロックの生成
//----------------------------------------------------------------------------------

// 使用された組み込み変数の宣言を出力する
static void ykslEmitBuiltinDeclarations(ykslContext *ctx, ykslStr *out)
{
    const char *emitted[YKSL_MAX_BUILTINS];
    int emittedCount = 0;
    char line[256];

    for (int pass = 0; pass < 3; pass++)   // attribute -> varying -> uniform の順
    {
        for (int i = 0; ykslBuiltins[i].name != NULL; i++)
        {
            if (!ctx->used[i]) continue;

            ykslBuiltinKind kind = ykslBuiltins[i].kind;
            if ((pass == 0) && (kind != YKSL_BI_ATTRIBUTE)) continue;
            if ((pass == 1) && (kind != YKSL_BI_VARYING)) continue;
            if ((pass == 2) && (kind != YKSL_BI_UNIFORM)) continue;

            const char *glName = ykslBuiltins[i].glName;

            int duplicated = 0;
            for (int j = 0; j < emittedCount; j++) if (strcmp(emitted[j], glName) == 0) duplicated = 1;
            if (duplicated) continue;
            emitted[emittedCount++] = glName;

            const char *qualifier = "uniform";
            if (kind == YKSL_BI_ATTRIBUTE) qualifier = ykslIsModern(ctx->version)? "in" : "attribute";
            else if (kind == YKSL_BI_VARYING)
            {
                if (!ykslIsModern(ctx->version)) qualifier = "varying";
                else qualifier = (ctx->stage == YKSL_STAGE_VERTEX)? "out" : "in";
            }

            const char *precision = "";
            if (ctx->coordHighp && ykslIsCoordBuiltin(glName)) precision = "YKSL_HIGHP ";

            snprintf(line, sizeof(line), "%s %s%s %s;\n", qualifier, precision, ykslBuiltins[i].type, glName);
            ykslStrAppend(out, line);
        }
    }
}

//----------------------------------------------------------------------------------
// 本体
//----------------------------------------------------------------------------------

// 指定した 1 ステージ分の GLSL を生成する
static char *ykslTranspile(const char *code, const ykslOptions *options, int stage)
{
    ykslErrorMessage[0] = '\0';

    ykslErrorMissingEntry = 0;

    if (code == NULL) { ykslSetError("ソースコードが NULL です"); return NULL; }

    ykslOptions opt = ykslResolveOptions(options);
    int version = opt.glslVersion;

    if ((version != 100) && (version != 120) && (version != 300) && (version != 330))
    {
        ykslSetError("未対応の GLSL バージョンです: %d (100 / 120 / 300 / 330)", version);
        return NULL;
    }

    if ((opt.fragmentPrecision < YKSL_PRECISION_DEFAULT) || (opt.fragmentPrecision > YKSL_PRECISION_HIGH))
    {
        ykslSetError("fragmentPrecision の値が不正です: %d", opt.fragmentPrecision);
        return NULL;
    }

    ykslTokenList list = { 0 };
    ykslTokenize(code, &list);

    ykslContext ctx = { 0 };
    ctx.options = opt;
    ctx.version = version;
    ctx.stage = stage;

    // 精度修飾子が意味を持つのは ES のみ (120 は非対応、330 は指定しても無視される)
    ctx.coordHighp = (opt.highPrecisionCoords && ykslIsES(version));
    ctx.highpMacro = (ctx.coordHighp ||
                      (ykslIsES(version) && (stage == YKSL_STAGE_FRAGMENT) && (opt.fragmentPrecision == YKSL_PRECISION_HIGH)));

    ykslStr userDecls, functions, mainBody, output;
    ykslStrInit(&userDecls);
    ykslStrInit(&functions);
    ykslStrInit(&mainBody);
    ykslStrInit(&output);

    ykslEmitter declEmitter = { &userDecls, 0, 1 };
    ykslEmitter funcEmitter = { &functions, 0, 1 };
    ykslEmitter bodyEmitter = { &mainBody, 1, 1 };

    const char *entryName = (stage == YKSL_STAGE_VERTEX)? "vertex" : "fragment";
    int entryFound = 0;
    int i = 0;

    while ((i < list.count) && !ctx.failed)
    {
        ykslToken *token = &list.items[i];

        // プリプロセッサ行
        if (token->type == YKSL_TOKEN_PREPROC)
        {
            ykslEmitRange(&declEmitter, &ctx, &list, i, i);
            i++;
            continue;
        }

        if (token->type == YKSL_TOKEN_IDENT)
        {
            // shader_type / render_mode は読み飛ばす (Godot 互換のため受け付ける)
            if ((strcmp(token->text, "shader_type") == 0) || (strcmp(token->text, "render_mode") == 0))
            {
                int end = ykslFindPunct(&list, i, ";");
                if (end < 0) { ykslSetError("';' がありません (行 %d)", token->line); ctx.failed = 1; break; }
                i = end + 1;
                continue;
            }

            // precision 文: ES のみ通す
            if (strcmp(token->text, "precision") == 0)
            {
                int end = ykslFindPunct(&list, i, ";");
                if (end < 0) { ykslSetError("';' がありません (行 %d)", token->line); ctx.failed = 1; break; }
                if (ykslIsES(version))
                {
                    ykslEmitRange(&declEmitter, &ctx, &list, i, end);
                    ykslStrAppend(&userDecls, "\n");
                    declEmitter.atLineStart = 1;
                }
                i = end + 1;
                continue;
            }

            // uniform 宣言
            if (strcmp(token->text, "uniform") == 0)
            {
                int end = ykslFindPunct(&list, i, ";");
                if (end < 0) { ykslSetError("uniform 宣言に ';' がありません (行 %d)", token->line); ctx.failed = 1; break; }

                int t = i + 1;
                while ((t < end) && (list.items[t].type == YKSL_TOKEN_IDENT) &&
                       ((strcmp(list.items[t].text, "lowp") == 0) || (strcmp(list.items[t].text, "mediump") == 0) ||
                        (strcmp(list.items[t].text, "highp") == 0))) t++;
                if ((t + 1 <= end) && (list.items[t].type == YKSL_TOKEN_IDENT) && (list.items[t + 1].type == YKSL_TOKEN_IDENT))
                    ykslAddSymbol(&ctx, list.items[t].text, list.items[t + 1].text);

                ykslEmitRange(&declEmitter, &ctx, &list, i, end);
                ykslStrAppend(&userDecls, "\n");
                declEmitter.atLineStart = 1;
                i = end + 1;
                continue;
            }

            // varying 宣言 (Godot 同様、頂点 -> フラグメントの受け渡し)
            if (strcmp(token->text, "varying") == 0)
            {
                int end = ykslFindPunct(&list, i, ";");
                if (end < 0) { ykslSetError("varying 宣言に ';' がありません (行 %d)", token->line); ctx.failed = 1; break; }

                const char *qualifier = "varying";
                if (ykslIsModern(version)) qualifier = (stage == YKSL_STAGE_VERTEX)? "out" : "in";

                if (!declEmitter.atLineStart) { ykslStrAppend(&userDecls, "\n"); declEmitter.atLineStart = 1; }
                ykslStrAppend(&userDecls, qualifier);
                ykslStrAppend(&userDecls, " ");
                declEmitter.atLineStart = 0;
                ykslEmitRange(&declEmitter, &ctx, &list, i + 1, end);
                ykslStrAppend(&userDecls, "\n");
                declEmitter.atLineStart = 1;
                i = end + 1;
                continue;
            }

            // const / struct 宣言
            if (strcmp(token->text, "const") == 0)
            {
                int end = ykslFindPunct(&list, i, ";");
                if (end < 0) { ykslSetError("const 宣言に ';' がありません (行 %d)", token->line); ctx.failed = 1; break; }
                ykslEmitRange(&declEmitter, &ctx, &list, i, end);
                ykslStrAppend(&userDecls, "\n");
                declEmitter.atLineStart = 1;
                i = end + 1;
                continue;
            }

            if (strcmp(token->text, "struct") == 0)
            {
                int open = ykslFindPunct(&list, i, "{");
                int close = (open >= 0)? ykslMatchBrace(&list, open) : -1;
                if (close < 0) { ykslSetError("struct の '}' がありません (行 %d)", token->line); ctx.failed = 1; break; }
                int end = ykslFindPunct(&list, close, ";");
                if (end < 0) end = close;
                ykslEmitRange(&declEmitter, &ctx, &list, i, end);
                ykslStrAppend(&userDecls, "\n");
                declEmitter.atLineStart = 1;
                i = end + 1;
                continue;
            }
        }

        // 関数定義 / プロトタイプ / グローバル変数
        {
            int lparen = -1;
            for (int p = i; p < list.count; p++)
            {
                if (ykslIsPunct(&list.items[p], ";")) break;
                if (ykslIsPunct(&list.items[p], "(")) { lparen = p; break; }
            }

            if (lparen < 0)     // グローバル変数など
            {
                int end = ykslFindPunct(&list, i, ";");
                if (end < 0) { ykslSetError("解釈できない記述です (行 %d): %s", token->line, token->text); ctx.failed = 1; break; }
                ykslEmitRange(&declEmitter, &ctx, &list, i, end);
                ykslStrAppend(&userDecls, "\n");
                declEmitter.atLineStart = 1;
                i = end + 1;
                continue;
            }

            int rparen = -1;
            {
                int depth = 0;
                for (int p = lparen; p < list.count; p++)
                {
                    if (ykslIsPunct(&list.items[p], "(")) depth++;
                    else if (ykslIsPunct(&list.items[p], ")")) { depth--; if (depth == 0) { rparen = p; break; } }
                }
            }
            if (rparen < 0) { ykslSetError("')' がありません (行 %d)", token->line); ctx.failed = 1; break; }

            if ((rparen + 1 < list.count) && ykslIsPunct(&list.items[rparen + 1], ";"))   // プロトタイプ宣言
            {
                ykslEmitRange(&funcEmitter, &ctx, &list, i, rparen + 1);
                ykslStrAppend(&functions, "\n");
                funcEmitter.atLineStart = 1;
                i = rparen + 2;
                continue;
            }

            int open = ykslFindPunct(&list, rparen, "{");
            int close = (open >= 0)? ykslMatchBrace(&list, open) : -1;
            if (close < 0) { ykslSetError("関数の '}' がありません (行 %d)", token->line); ctx.failed = 1; break; }

            const char *funcName = (lparen > 0)? list.items[lparen - 1].text : "";

            if (strcmp(funcName, entryName) == 0)
            {
                if (entryFound) { ykslSetError("%s() が複数定義されています", entryName); ctx.failed = 1; break; }
                entryFound = 1;
                if (open + 1 <= close - 1) ykslEmitRange(&bodyEmitter, &ctx, &list, open + 1, close - 1);
            }
            else if ((strcmp(funcName, "vertex") == 0) || (strcmp(funcName, "fragment") == 0))
            {
                // 反対のステージのエントリ関数: 共通ソースを許すため読み飛ばす
            }
            else if (strcmp(funcName, "main") == 0)
            {
                ykslSetError("main() は予約されています。%s() を使用してください", entryName);
                ctx.failed = 1;
                break;
            }
            else
            {
                if (!funcEmitter.atLineStart) { ykslStrAppend(&functions, "\n"); funcEmitter.atLineStart = 1; }
                ykslEmitRange(&funcEmitter, &ctx, &list, i, close);
                ykslStrAppend(&functions, "\n");
                funcEmitter.atLineStart = 1;
            }

            i = close + 1;
            continue;
        }
    }

    char *result = NULL;

    if (!ctx.failed && !entryFound)
    {
        ykslSetError("void %s() が見つかりません", entryName);
        ykslErrorMissingEntry = 1;
        ctx.failed = 1;
    }

    if (!ctx.failed)
    {
        // 頂点シェーダで POSITION 未書き込みなら既定の変換を補う
        if (stage == YKSL_STAGE_VERTEX)
        {
            int posIndex = ykslFindBuiltin("POSITION", YKSL_STAGE_VERTEX);
            if ((posIndex >= 0) && !ctx.used[posIndex])
            {
                ctx.used[posIndex] = 1;
                ctx.used[ykslFindBuiltin("MVP_MATRIX", YKSL_STAGE_VERTEX)] = 1;
                ctx.used[ykslFindBuiltin("VERTEX", YKSL_STAGE_VERTEX)] = 1;
                ykslStrAppend(&mainBody, "\n    gl_Position = mvp*vec4(vertexPosition, 1.0);");
            }
        }

        // ヘッダ
        switch (version)
        {
            case 100: ykslStrAppend(&output, "#version 100\n"); break;
            case 120: ykslStrAppend(&output, "#version 120\n"); break;
            case 300: ykslStrAppend(&output, "#version 300 es\n"); break;
            case 330: ykslStrAppend(&output, "#version 330\n"); break;
            default: break;
        }
        ykslStrAppend(&output, "// Generated by yksl (ykSL -> GLSL transpiler)\n\n");

        // highp フォールバック用マクロ
        // GLSL ES 1.00 のフラグメントシェーダは highp が任意対応なので、
        // GL_FRAGMENT_PRECISION_HIGH が無い端末では mediump に落とす
        if (ctx.highpMacro)
        {
            if ((version == 100) && (stage == YKSL_STAGE_FRAGMENT))
            {
                ykslStrAppend(&output, "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
                                       "    #define YKSL_HIGHP highp\n"
                                       "#else\n"
                                       "    #define YKSL_HIGHP mediump\n"
                                       "#endif\n\n");
            }
            else ykslStrAppend(&output, "#define YKSL_HIGHP highp\n\n");   // 頂点シェーダと ES 3.00 は highp 必須対応
        }

        if (ykslIsES(version))
        {
            if (stage == YKSL_STAGE_VERTEX) ykslStrAppend(&output, "precision highp float;\n\n");
            else switch (opt.fragmentPrecision)
            {
                case YKSL_PRECISION_LOW:  ykslStrAppend(&output, "precision lowp float;\n\n"); break;
                case YKSL_PRECISION_HIGH: ykslStrAppend(&output, "precision YKSL_HIGHP float;\n\n"); break;
                default:                  ykslStrAppend(&output, "precision mediump float;\n\n"); break;
            }
        }

        ykslEmitBuiltinDeclarations(&ctx, &output);

        // フラグメント出力
        if (stage == YKSL_STAGE_FRAGMENT)
        {
            int colorIndex = ykslFindBuiltin("COLOR", YKSL_STAGE_FRAGMENT);
            if ((colorIndex >= 0) && ctx.used[colorIndex] && ykslIsModern(version))
                ykslStrAppend(&output, "out vec4 finalColor;\n");
        }

        if (userDecls.length > 0) { ykslStrAppend(&output, "\n"); ykslStrAppend(&output, userDecls.data); }
        if (functions.length > 0) { ykslStrAppend(&output, "\n"); ykslStrAppend(&output, functions.data); }

        ykslStrAppend(&output, "\nvoid main()\n{\n");
        if (mainBody.length > 0)
        {
            ykslStrAppend(&output, mainBody.data);
            ykslStrAppend(&output, "\n");
        }
        ykslStrAppend(&output, "}\n");

        if (output.data != NULL)
        {
            result = ykslStrDupN(output.data, output.length);
            if (result == NULL) ykslSetError("メモリの確保に失敗しました");
        }
        else ykslSetError("メモリの確保に失敗しました");
    }

    ykslStrFree(&userDecls);
    ykslStrFree(&functions);
    ykslStrFree(&mainBody);
    ykslStrFree(&output);
    ykslFreeSymbols(&ctx);
    ykslTokenListFree(&list);

    return result;
}

bool ykslShaderTranspile(const char *code, const ykslOptions *options, char **vsCode, char **fsCode)
{
    if (vsCode != NULL) *vsCode = NULL;
    if (fsCode != NULL) *fsCode = NULL;

    ykslErrorMessage[0] = '\0';

    if ((vsCode == NULL) && (fsCode == NULL))
    {
        ykslSetError("出力先が両方 NULL です");
        return false;
    }

    if (vsCode != NULL)
    {
        *vsCode = ykslTranspile(code, options, YKSL_STAGE_VERTEX);
        if (*vsCode == NULL) return false;
    }

    if (fsCode != NULL)
    {
        *fsCode = ykslTranspile(code, options, YKSL_STAGE_FRAGMENT);
        if (*fsCode == NULL)
        {
            if (vsCode != NULL) { ykslUnloadShaderCode(*vsCode); *vsCode = NULL; }
            return false;
        }
    }

    return true;
}

#if defined(YKSL_RAYLIB_SUPPORT)
Shader ykslLoadShaderFromMemory(const char *code, const ykslOptions *options)
{
    char *vs = NULL;
    char *fs = NULL;

    // vertex()/fragment() の一方しか無いソースも許す (無い側は raylib の既定シェーダ)
    if (!ykslShaderTranspile(code, options, &vs, NULL) && !ykslErrorMissingEntry)
        TraceLog(LOG_WARNING, "YKSL: vertex: %s", ykslGetTranspileError());

    if (!ykslShaderTranspile(code, options, NULL, &fs) && !ykslErrorMissingEntry)
        TraceLog(LOG_WARNING, "YKSL: fragment: %s", ykslGetTranspileError());

    Shader shader = LoadShaderFromMemory(vs, fs);

    ykslUnloadShaderCode(vs);
    ykslUnloadShaderCode(fs);

    return shader;
}
#endif

#endif // YKSL_IMPLEMENTATION
