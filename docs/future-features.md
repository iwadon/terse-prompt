# terse-prompt 将来機能提案

**作成日**: 2025-11-11
**対象**: v2.0以降の機能拡張

---

## 1. 概要

このドキュメントでは、terse-promptを自作プログラミング言語のREPLなど、より高度なユースケースで利用する際に有用となる将来機能を定義します。

---

## 2. 提案機能一覧

### 2.1 構文検証コールバック (Syntax Validation Callback)

**優先度**: MUST（プログラミング言語REPLの基本機能）

#### 2.1.1 概要

ユーザーがEnterキーを押した際、入力が「完結しているか」を判定し、以下のいずれかの動作を選択できる機能：

- **ACCEPT**: 入力を確定して呼び出し元に返す
- **CONTINUE**: 改行して入力を継続（マルチラインモード）
- **REJECT**: エラーを表示して編集を継続

#### 2.1.2 ユースケース

**Ruby/Pythonスタイル**:
```ruby
if x > 0    # ← Enterを押しても確定せず、改行して続きを受け付ける
  puts x
end         # ← ここでEnterを押すと確定
```

**カッコの対応チェック**:
```python
result = calculate(
    10,
    20,     # ← Enterで改行
    30
)           # ← 対応する閉じカッコでEnterを押すと確定
```

**セミコロンチェック（C言語風）**:
```c
int x = 10   # ← セミコロンがないのでEnterで改行
             # （または警告を表示）
```

#### 2.1.3 API設計

```c
/**
 * @brief 検証結果の列挙型
 */
typedef enum tprompt_validation_result {
    TPROMPT_VALIDATION_ACCEPT = 0,   /**< 入力を確定して返す */
    TPROMPT_VALIDATION_CONTINUE = 1, /**< 改行して入力を継続（マルチラインモードのみ） */
    TPROMPT_VALIDATION_REJECT = 2    /**< エラーを表示して編集を継続 */
} tprompt_validation_result_t;

/**
 * @brief 検証コールバック関数型
 *
 * Enterキーが押された際に呼び出され、入力の完結性をチェックします。
 *
 * @param text 現在の入力テキスト（NUL終端）
 * @param length テキストのバイト長
 * @param user_data ユーザー定義データ
 * @return 検証結果
 */
typedef tprompt_validation_result_t (*tprompt_validation_fn)(
    const char *text,
    size_t length,
    void *user_data
);
```

#### 2.1.4 使用例

**Ruby REPL風の実装**:
```c
tprompt_validation_result_t validate_ruby(const char *text, size_t length, void *user_data) {
    // カッコの対応をチェック
    int paren_count = 0;
    int brace_count = 0;

    for (size_t i = 0; i < length; i++) {
        if (text[i] == '(') paren_count++;
        if (text[i] == ')') paren_count--;
        if (text[i] == '{') brace_count++;
        if (text[i] == '}') brace_count--;
    }

    // カッコが閉じていない場合は継続
    if (paren_count > 0 || brace_count > 0) {
        return TPROMPT_VALIDATION_CONTINUE;
    }

    // キーワードチェック（簡易版）
    if (strstr(text, "if ") && !strstr(text, "end")) {
        return TPROMPT_VALIDATION_CONTINUE;
    }

    return TPROMPT_VALIDATION_ACCEPT;
}

// 使用例
tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
opts.validation_callback = validate_ruby;
opts.validation_user_data = NULL;
```

#### 2.1.5 `tprompt_options_t`への追加

```c
typedef struct tprompt_options {
    // ... 既存のフィールド ...
    tprompt_validation_fn validation_callback;  /**< 検証コールバック（NULL=無効） */
    void *validation_user_data;                 /**< 検証コールバックのユーザーデータ */
} tprompt_options_t;
```

#### 2.1.6 実装上の注意

- **マルチラインモードのみ**: `TPROMPT_VALIDATION_CONTINUE`はマルチラインモードでのみ有効
- **シングルラインモード**: `CONTINUE`が返された場合は`ACCEPT`として扱う
- **デフォルト動作**: コールバックがNULLの場合は常に`ACCEPT`

---

### 2.2 構文ハイライト (Syntax Highlighting)

**優先度**: SHOULD（視認性向上）

#### 2.2.1 概要

入力中にキーワード、文字列リテラル、コメントなどを色分け表示する機能。

#### 2.2.2 ユースケース

```python
def calculate(x, y):  # キーワード "def" を青色、文字列はなし、コメント "#..." を灰色
    return x + y      # キーワード "return" を青色
```

#### 2.2.3 API設計

```c
/**
 * @brief ハイライトスパン構造体
 */
typedef struct tprompt_highlight_span {
    size_t start;    /**< 開始位置（バイトオフセット） */
    size_t length;   /**< 長さ（バイト数） */
    int color;       /**< ANSI色コードまたは属性フラグ */
} tprompt_highlight_span_t;

/**
 * @brief ハイライトコールバック関数型
 *
 * 文字の入力・削除の度に呼び出され、ハイライト情報を返します。
 *
 * @param text 現在の入力テキスト（NUL終端）
 * @param length テキストのバイト長
 * @param out_count スパン配列の要素数（出力）
 * @param user_data ユーザー定義データ
 * @return ハイライトスパン配列（malloc確保、ライブラリがfreeする）
 */
typedef tprompt_highlight_span_t* (*tprompt_highlight_fn)(
    const char *text,
    size_t length,
    size_t *out_count,
    void *user_data
);
```

#### 2.2.4 色定義

```c
// ANSI色コード定数
#define TPROMPT_COLOR_RESET       0
#define TPROMPT_COLOR_BLACK       30
#define TPROMPT_COLOR_RED         31
#define TPROMPT_COLOR_GREEN       32
#define TPROMPT_COLOR_YELLOW      33
#define TPROMPT_COLOR_BLUE        34
#define TPROMPT_COLOR_MAGENTA     35
#define TPROMPT_COLOR_CYAN        36
#define TPROMPT_COLOR_WHITE       37

// 属性フラグ
#define TPROMPT_ATTR_BOLD         (1 << 8)
#define TPROMPT_ATTR_DIM          (1 << 9)
#define TPROMPT_ATTR_UNDERLINE    (1 << 10)
```

#### 2.2.5 使用例

```c
tprompt_highlight_span_t* highlight_python(
    const char *text,
    size_t length,
    size_t *out_count,
    void *user_data
) {
    // 簡易実装例: "def", "return" をキーワードとして青色に
    static const char *keywords[] = {"def", "return", "if", "else", NULL};

    // スパン配列を動的確保
    tprompt_highlight_span_t *spans = malloc(sizeof(*spans) * 100);
    *out_count = 0;

    for (size_t i = 0; keywords[i]; i++) {
        const char *pos = text;
        size_t kw_len = strlen(keywords[i]);

        while ((pos = strstr(pos, keywords[i])) != NULL) {
            spans[*out_count].start = pos - text;
            spans[*out_count].length = kw_len;
            spans[*out_count].color = TPROMPT_COLOR_BLUE | TPROMPT_ATTR_BOLD;
            (*out_count)++;
            pos += kw_len;
        }
    }

    return spans;
}
```

#### 2.2.6 実装上の注意

- **パフォーマンス**: 大きなテキストでは呼び出し頻度が高いため、効率的な実装が必要
- **UTF-8対応**: `start`と`length`はバイトオフセット
- **重複スパン**: 重複する場合は後のスパンが優先

---

### 2.3 カスタムプロンプト (Continuation Prompt)

**優先度**: SHOULD（継続行の明示）

#### 2.3.1 概要

複数行入力時に、2行目以降に異なるプロンプトを表示する機能。

#### 2.3.2 ユースケース

```python
>>> if x > 0:       # 1行目: ">>> "
...     print(x)    # 2行目以降: "... "
...     print("ok")
```

#### 2.3.3 API設計

```c
typedef struct tprompt_options {
    const char *prompt;              /**< 最初の行のプロンプト */
    const char *continuation_prompt; /**< 2行目以降のプロンプト（NULL=同じプロンプトを使用） */
    // ... 既存のフィールド ...
} tprompt_options_t;
```

#### 2.3.4 使用例

```c
tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
opts.prompt = ">>> ";
opts.continuation_prompt = "... ";
```

---

### 2.4 インデント自動調整 (Auto-Indentation)

**優先度**: COULD（快適性向上）

#### 2.4.4 概要

前行の内容に基づいて自動的にインデントを挿入する機能。

#### 2.4.2 ユースケース

```python
if x > 0:         # ← Enterを押すと...
    print(x)      # ← 自動的に4スペースインデント
```

#### 2.4.3 API設計

```c
/**
 * @brief インデントコールバック関数型
 *
 * 改行時に呼び出され、次の行のインデントレベル（スペース数）を返します。
 *
 * @param text 現在の入力テキスト（NUL終端）
 * @param cursor_pos カーソル位置（バイトオフセット）
 * @param user_data ユーザー定義データ
 * @return インデントレベル（スペース数、0=インデントなし）
 */
typedef int (*tprompt_indent_fn)(
    const char *text,
    size_t cursor_pos,
    void *user_data
);
```

#### 2.4.4 使用例

```c
int indent_python(const char *text, size_t cursor_pos, void *user_data) {
    // カーソルより前の最後の行を取得
    const char *line_start = text;
    for (size_t i = 0; i < cursor_pos; i++) {
        if (text[i] == '\n') {
            line_start = &text[i + 1];
        }
    }

    // 現在の行のインデントレベルを計算
    int current_indent = 0;
    while (line_start[current_indent] == ' ') {
        current_indent++;
    }

    // 行末が ':' ならインデント増加
    if (cursor_pos > 0 && text[cursor_pos - 1] == ':') {
        return current_indent + 4;
    }

    return current_indent;
}
```

---

### 2.5 ブラケットマッチング表示 (Bracket Matching)

**優先度**: COULD（視認性向上）

#### 2.5.1 概要

カーソルが`()`、`{}`、`[]`の上にある時、対応する括弧を強調表示する機能。

#### 2.5.2 実装方針

- ハイライトシステム（2.2）の拡張として実装
- カーソル位置の変更時に対応括弧を検索
- 対応括弧が見つかった場合、両方を強調表示

---

### 2.6 行番号表示 (Line Numbers)

**優先度**: COULD（デバッグ支援）

#### 2.6.1 概要

複数行入力時に、各行の左端に行番号を表示するオプション。

#### 2.6.2 API設計

```c
#define TPROMPT_FLAG_SHOW_LINE_NUMBERS (1 << 3)
```

#### 2.6.3 表示例

```
  1 | def calculate(x, y):
  2 |     return x + y
  3 |
```

---

### 2.7 エラー位置マーカー (Error Marker)

**優先度**: COULD（デバッグ支援）

#### 2.7.1 概要

構文エラーがあった場合、該当箇所に波線や矢印を表示できる機能。

#### 2.7.2 API設計

```c
/**
 * @brief エラー位置を表示
 *
 * 指定位置にエラーマーカーを表示します。
 * 次回の入力取得時にクリアされます。
 *
 * @param handle セッションハンドル
 * @param byte_offset エラー位置（バイトオフセット）
 * @param message エラーメッセージ（NULLの場合はマーカーのみ）
 */
void tprompt_show_error(
    tprompt_handle_t handle,
    size_t byte_offset,
    const char *message
);
```

#### 2.7.3 表示例

```
>>> result = calculate(10, 20, 30)
                                 ^
SyntaxError: Too many arguments
```

---

## 3. 実装優先順位

プログラミング言語REPLとしての利用を想定した場合の推奨実装順序：

### Phase 1: 基本REPL機能（v2.0）
1. **構文検証コールバック** (2.1) - 不完全な入力での自動改行
2. **カスタムプロンプト** (2.3) - 継続行の明示

### Phase 2: 視認性向上（v2.1）
3. **構文ハイライト** (2.2) - キーワード・文字列の色分け

### Phase 3: 快適性向上（v2.2）
4. **インデント自動調整** (2.4) - 自動インデント
5. **ブラケットマッチング** (2.5) - 対応括弧の強調

### Phase 4: デバッグ支援（v2.3）
6. **行番号表示** (2.6) - 行番号の表示
7. **エラー位置マーカー** (2.7) - エラー箇所の明示

---

## 4. 既存機能との関係

### 4.1 補完機能との統合

構文ハイライト（2.2）と補完機能は密接に関連：
- 補完候補リストにも構文ハイライトを適用
- 補完中のテキストもハイライト対象

### 4.2 キーバインディングとの統合

インデント自動調整（2.4）は既存のキーバインディングシステムと連携：
- Enterキーで改行時に自動インデント
- Tabキーで手動インデント調整

### 4.3 履歴機能との統合

構文検証（2.1）と履歴機能の関係：
- `ACCEPT`された入力のみ履歴に追加
- `REJECT`された入力は履歴に追加しない

---

## 5. API互換性

### 5.1 後方互換性の維持

すべての新機能は以下の方針で追加：
- 既存APIの動作を変更しない
- 新しいフィールド/コールバックはNULLで無効化
- デフォルト動作は現行バージョンと同一

### 5.2 オプション構造体の拡張

```c
typedef struct tprompt_options {
    // v1.0: 既存フィールド
    const char *prompt;
    const char *history_file;
    // ... 省略 ...

    // v2.0: Phase 1追加
    const char *continuation_prompt;
    tprompt_validation_fn validation_callback;
    void *validation_user_data;

    // v2.1: Phase 2追加
    tprompt_highlight_fn highlight_callback;
    void *highlight_user_data;

    // v2.2: Phase 3追加
    tprompt_indent_fn indent_callback;
    void *indent_user_data;
} tprompt_options_t;
```

---

## 6. パフォーマンス考慮事項

### 6.1 構文ハイライトの最適化

- **キャッシング**: テキストが変更されない限り再計算しない
- **差分更新**: 変更箇所のみ再ハイライト
- **遅延実行**: 高速タイピング中は更新を遅延

### 6.2 検証コールバックの効率化

- **部分検証**: カーソル位置周辺のみチェック
- **構文木キャッシュ**: パーサー結果を再利用

---

## 7. 参考実装

### 7.1 既存REPL

- **Python**: `>>>` と `...` プロンプト、自動インデント
- **Ruby (irb)**: 構文検証、カッコ対応チェック
- **Node.js**: 構文ハイライト、ブラケットマッチング
- **IPython**: 行番号表示、構文ハイライト

### 7.2 エディタライブラリ

- **GNU readline**: 基本的な行編集機能
- **linenoise**: 軽量な代替実装
- **replxx**: 構文ハイライト対応

---

## 8. 変更履歴

| 日付 | バージョン | 変更内容 |
|------|-----------|---------|
| 2025-11-11 | 0.1 | 初版作成（将来機能提案） |
