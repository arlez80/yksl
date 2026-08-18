# Godot風シェーダ言語GLSLトランスパイラー

ykSLとは、GLSLのバージョン差（`attribute`/`varying` vs `in`/`out`、`texture2D` vs `texture`、`gl_FragColor` vs `out vec4`、`precision` 修飾子など）を吸収し、
1つのソースを raylib の `GLSL_VERSION` に合わせて出し分けるためのライブラリです。

例：

```c
#define YKSL_IMPLEMENTATION
#include "yksl.h"

char *vs = NULL, *fs = NULL;

ykslOptions options = ykslGetDefaultOptions();
options.glslVersion = GLSL_VERSION;             // 100 / 120 / 300 / 330

if (ykslShaderTranspile(code, &options, &vs, &fs))
{
    // ...使う...
    ykslUnloadShaderCode(vs);
    ykslUnloadShaderCode(fs);
}
else TraceLog(LOG_WARNING, "%s", ykslGetTranspileError());
```

## API

```c
ykslOptions ykslGetDefaultOptions(void);
bool ykslShaderTranspile(const char *code, const ykslOptions *options, char **vsCode, char **fsCode);
void ykslUnloadShaderCode(char *code);
const char *ykslGetTranspileError(void);

// YKSL_RAYLIB_SUPPORT 定義時のみ (内部で transpile -> LoadShaderFromMemory)
Shader ykslLoadShaderFromMemory(const char *vsCode, const char *fsCode, const ykslOptions *options);
```

| 引数 | 説明 |
|---|---|
| `code` | ykSL ソース。Godotのように `vertex()` と `fragment()` を両方を記述してよい。 |
| `options` | トランスパイル設定。**`NULL` なら全項目が既定値** |
| `vsCode` | 頂点シェーダの出力先。**`NULL` なら生成しない** |
| `fsCode` | フラグメントシェーダの出力先。**`NULL` なら生成しない** |

成功で `true`。失敗時は `false` を返し、出力先はすべて `NULL` に戻されます（途中まで確保したものはライブラリ側で解放）。

## オプション

```c
typedef struct ykslOptions {
    int  glslVersion;           // GLSLバージョン
    int  fragmentPrecision;     // フラグメントシェーダでのfloat精度。ES以外では無視
    bool highPrecisionCoords;   // 座標計算を高精度に（ESのみ）
} ykslOptions;
```

| 項目 | 既定値 | 説明 |
|---|---|---|
| `glslVersion` | `330` | `100` / `120` / `300` / `330`。`0` を渡すと未設定として既定値になる |
| `fragmentPrecision` | `YKSL_PRECISION_DEFAULT`(=mediump) | フラグメントの `precision ~ float;`。`LOW`/`MEDIUM`/`HIGH` |
| `highPrecisionCoords` | `false` | 座標系の組み込み変数だけを highp にする |

**必ず `ykslGetDefaultOptions()` で初期化してから項目を変更してください。**
今後オプションが増えても、この書き方なら呼び出し側のコードは変更不要です（`ykslOptions o = {0};` でも `glslVersion == 0` は既定値として扱われます）。

### highPrecisionCoords（ES2.0 の mediump(fp16) 対策）

GLSL ES 1.00 のフラグメントシェーダでは **highp のサポートが任意**で、既定精度は各エンジンが `precision mediump float;` を入れるのが慣例です。
そして GLSL ES の `mediump` は仕様上 **範囲 ±2^14（=16384）、相対精度 2^-10** しか保証されず、多くのモバイル GPU は実際に fp16 で処理します。

このため

- 大きなテクスチャアトラスの UV、ワールド座標、`gl_FragCoord` 由来の計算
- `time` を足し込む演算（数分でガタつく／破綻する）
- `normalize()`/`length()` の途中結果のオーバーフロー

といった「座標系」の値が壊れます。かといって全部 `highp` と書くと、highp 非対応の端末で **コンパイルエラー**になります。

`highPrecisionCoords = true` にすると、次のフォールバックマクロを出力し、

```glsl
#ifdef GL_FRAGMENT_PRECISION_HIGH
    #define YKSL_HIGHP highp
#else
    #define YKSL_HIGHP mediump
#endif
```

座標系の組み込み変数（`vertexPosition` / `vertexTexCoord` / `vertexTexCoord2` / `fragTexCoord` / `fragPosition` / `mvp` / `matModel` / `matView` / `matProjection` / `matNormal` / `time`）だけを `YKSL_HIGHP` 修飾して宣言します。色や法線は mediump のままなので速度を大きく落としません。

```glsl
varying YKSL_HIGHP vec2 fragTexCoord;
uniform YKSL_HIGHP float time;
varying vec4 fragColor;              // 色はそのまま
```

ユーザーが自分で書いた `highp` も、このモードでは自動的に `YKSL_HIGHP` に置換されるので、highp 非対応端末でもコンパイルが通ります。
頂点シェーダと ES 3.00 は highp が必須対応なので `#define YKSL_HIGHP highp` を出力するだけです。
デスクトップ（120 / 330）ではこのオプションは何もしません。加えて **GLSL 1.20 は精度修飾子自体を持たない**ため、ソース中の `highp`/`mediump`/`lowp` は自動で除去されます。

## 言語仕様 (ykSL)

Godot と同じく、`main()` ではなく **`void vertex()` / `void fragment()`** を書きます。
1つのソースに両方書けるようになっており、トランスパイル時に必要に応じて各関数が `main()` に展開されます。

```glsl
shader_type canvas_item;      // 受け付けるが無視します

uniform vec4 tint;
varying vec3 worldPos;        // vertex → fragment

float wave(float x) { return sin(x)*0.5 + 0.5; }

void vertex() {
    worldPos = (MODEL_MATRIX*vec4(VERTEX, 1.0)).xyz;
    FRAG_UV = UV;
    POSITION = MVP_MATRIX*vec4(VERTEX, 1.0);   // 省略すると自動で補われる
}

void fragment() {
    COLOR = texture(TEXTURE, UV)*MODULATE*tint;
}
```

### 組み込み変数（raylib の既定名にマップされます）

**vertex ステージ（頂点属性）**

| ykSL | GLSL | 型 |
|---|---|---|
| `VERTEX` | `vertexPosition` | vec3 |
| `UV` / `UV2` | `vertexTexCoord` / `vertexTexCoord2` | vec2 |
| `NORMAL` | `vertexNormal` | vec3 |
| `TANGENT` | `vertexTangent` | vec4 |
| `COLOR` | `vertexColor` | vec4 |
| `POSITION` | `gl_Position` | 出力 |
| `POINT_SIZE` | `gl_PointSize` | 出力 |

**varying（両ステージ共通）**: `FRAG_UV`→`fragTexCoord`, `FRAG_COLOR`→`fragColor`, `FRAG_NORMAL`→`fragNormal`, `FRAG_POSITION`→`fragPosition`
（fragment 側では `UV` が `fragTexCoord` の別名になります）

**fragment ステージ**: `COLOR` は **出力色**（`gl_FragColor` / `out vec4 finalColor` に切り替わる）、`FRAGCOORD`→`gl_FragCoord`, `FRONT_FACING`, `POINT_COORD`

**uniform**: `TEXTURE`/`TEXTURE1`/`TEXTURE2`→`texture0..2`, `MODULATE`(=`COLOR_DIFFUSE`)→`colDiffuse`,
`MVP_MATRIX`→`mvp`, `MODEL_MATRIX`→`matModel`, `VIEW_MATRIX`→`matView`, `PROJECTION_MATRIX`→`matProjection`, `NORMAL_MATRIX`→`matNormal`,
`TIME`→`time`（**これだけは raylib が自動設定しないので `SetShaderValue()` で毎フレーム渡す**）

使用した組み込み変数の宣言だけが自動生成されるため、未使用の attribute/uniform は出力されません。

### バージョン差の自動処理

| 項目 | 100 / 120 | 300 / 330 |
|---|---|---|
| 頂点属性 | `attribute` | `in` |
| varying | `varying` | vertex: `out` / fragment: `in` |
| 出力色 | `gl_FragColor` | `out vec4 finalColor;` |
| テクスチャ | `texture2D` / `textureCube` / `texture3D`（サンプラ型から自動判別） | `texture` |
| precision | ES(100/300) のみ付与、120/330 では `precision` 文を除去 | — |

`uniform` / `const` / `struct` / 自作関数 / `#define` などの前処理行はそのまま透過します。

### 制限

- 型チェックは行いません（GLSL コンパイラのエラーがそのまま出ます）。トークン単位の書き換え＋構造解析のみです。
- `texelFetch()` は 100/120 でエラー、`textureLod()` は 100/120 のフラグメントシェーダでエラーになります。
- Godot の `ALPHA`, `SCREEN_TEXTURE`, ライティング関数などは未対応です。
- `main()` は予約語（`vertex()`/`fragment()` を使う）。

## ビルド

ライブラリ本体は `yksl.h` のみ。1 つの .c で `#define YKSL_IMPLEMENTATION` してから include します。

```
gcc test.c -o test              # トランスパイル結果の確認
gcc example/main.c -o example -lraylib -lm -ldl -lpthread -lGL
```

## License

MIT License

Copyright (C) 2026 あるる（きのもと 結衣） @arlez80

