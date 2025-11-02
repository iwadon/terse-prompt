# terse-prompt API設計書

**プロジェクト**: terse-prompt
**作成日**: 2025-11-02
**バージョン**: 0.1

---

## 1. 概要

terse-promptは、CLI REPLツール向けの複数行対応テキスト入力ライブラリです。本ドキュメントでは、以下の2つのAPIスタイルを定義します：

1. **シンプルラッパーAPI**: 1関数で全機能を使える簡易インターフェース
2. **フレームワークAPI**: カスタマイズ可能な低レベルインターフェース

---

## 2. API命名規則

- **関数プレフィックス**: `tprompt_`
- **構造体型**: `tprompt_*_t`
- **列挙型定数**: `TPROMPT_*`
- **エラーコード**: `TPROMPT_ERROR_*`

---

## 3. データ構造

### 3.1 ハンドル型

```c
typedef struct tprompt_handle *tprompt_handle_t;
```

**説明**:
- 編集セッションの状態を保持する不透明ポインタ
- `tprompt_open()`で生成、`tprompt_close()`で破棄
- 内部でterseハンドルを管理

### 3.2 オプション構造体

```c
typedef struct tprompt_options {
    const char *prompt;           // プロンプト文字列（NULL可、デフォルト: "> "）
    const char *history_file;     // 履歴ファイルパス（NULL=メモリのみ）
    size_t max_input_size;        // 最大入力サイズ（0=無制限）
    size_t max_history_size;      // 最大履歴保存数（0=無制限）
    tprompt_completion_fn completion_callback; // 補完コールバック（NULL=無効）
    void *completion_user_data;   // 補完コールバックのユーザーデータ
    const char *completion_prefixes; // 補完トリガー文字列（例: "/@"、NULL=無効）
    terse_handle_t terse_handle;  // 既存のterseハンドル（NULL=自動作成）
    int flags;                    // 動作フラグ（TPROMPT_FLAG_*）
} tprompt_options_t;
```

**フラグ定数**:
```c
#define TPROMPT_FLAG_MULTILINE        (1 << 0)  // 複数行モード有効（デフォルト）
#define TPROMPT_FLAG_DISABLE_HISTORY  (1 << 1)  // 履歴機能を無効化
#define TPROMPT_FLAG_NO_AUTO_SAVE     (1 << 2)  // 履歴の自動保存を無効化
```

**デフォルト初期化マクロ**:
```c
#define TPROMPT_OPTIONS_DEFAULT { \
    .prompt = "> ", \
    .history_file = NULL, \
    .max_input_size = 1024 * 1024, /* 1MB */ \
    .max_history_size = 100, \
    .completion_callback = NULL, \
    .completion_user_data = NULL, \
    .completion_prefixes = NULL, \
    .terse_handle = NULL, \
    .flags = TPROMPT_FLAG_MULTILINE \
}
```

### 3.3 補完コールバック型

```c
typedef struct tprompt_completion_result {
    char **candidates;  // 候補文字列配列（NULL終端）
    size_t count;       // 候補数
} tprompt_completion_result_t;

typedef tprompt_completion_result_t (*tprompt_completion_fn)(
    const char *text,        // 現在の入力テキスト全体
    size_t cursor_pos,       // カーソル位置（バイトオフセット）
    const char *prefix_char, // トリガーとなったプレフィックス文字
    void *user_data          // ユーザーデータ
);
```

**メモリ管理**:
- コールバック関数は`candidates`配列と各文字列をmallocで確保
- terse-promptが`tprompt_free_completion_result()`で解放

**補完候補解放関数**:
```c
void tprompt_free_completion_result(tprompt_completion_result_t *result);
```

### 3.4 エラーコード

```c
typedef enum tprompt_error_category {
    TPROMPT_ERROR_NONE = 0,          // エラーなし
    TPROMPT_ERROR_INVALID_ARGS,      // 無効な引数
    TPROMPT_ERROR_IO,                // I/Oエラー（errno参照）
    TPROMPT_ERROR_MEMORY,            // メモリ不足
    TPROMPT_ERROR_TERSE,             // terse APIエラー
    TPROMPT_ERROR_HISTORY_FILE,      // 履歴ファイル読み書きエラー
    TPROMPT_ERROR_ENCODING,          // UTF-8エンコーディングエラー
    TPROMPT_ERROR_SIZE_LIMIT         // サイズ制限超過
} tprompt_error_category_t;

typedef struct tprompt_error_info {
    tprompt_error_category_t category; // エラーカテゴリ
    int code;                          // 詳細コード（errno相当）
    char message[256];                 // エラーメッセージ
} tprompt_error_info_t;
```

---

## 4. シンプルラッパーAPI

### 4.1 `tprompt()`

```c
char *tprompt(const char *prompt_text);
```

**説明**:
- 1回の呼び出しで1行（または複数行）の入力を取得
- 内部で一時的なハンドルを作成・破棄
- 履歴はメモリのみ（ファイル保存なし）
- 補完機能なし
- Emacs風キーバインド有効

**引数**:
- `prompt_text`: プロンプト文字列（NULLの場合 `"> "` を使用）

**戻り値**:
- 成功: ユーザーが入力した文字列（malloc確保、呼び出し側が`free()`する）
- 失敗: `NULL`（エラー情報はerrnoに格納）
- EOF: `NULL`（Ctrl+D等）

**使用例**:
```c
char *input = tprompt("Enter command: ");
if (input) {
    printf("You entered: %s\n", input);
    free(input);
} else {
    if (errno == 0) {
        printf("EOF\n");
    } else {
        perror("tprompt");
    }
}
```

---

## 5. フレームワークAPI

### 5.1 初期化・クリーンアップ

#### 5.1.1 `tprompt_open()`

```c
tprompt_handle_t tprompt_open(const tprompt_options_t *options);
```

**説明**:
- 編集セッションを開始し、ハンドルを返す
- 履歴ファイルが指定されている場合は読み込み
- terseハンドルが未指定の場合は自動作成

**引数**:
- `options`: オプション構造体（NULLの場合はデフォルト設定を使用）

**戻り値**:
- 成功: ハンドル
- 失敗: `NULL`（エラー情報は`tprompt_get_last_error()`で取得）

**使用例**:
```c
tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
opts.history_file = "~/.myapp_history";
opts.max_history_size = 1000;

tprompt_handle_t handle = tprompt_open(&opts);
if (!handle) {
    tprompt_error_info_t err = tprompt_get_last_error(NULL);
    fprintf(stderr, "Error: %s\n", err.message);
    return 1;
}
```

#### 5.1.2 `tprompt_close()`

```c
void tprompt_close(tprompt_handle_t handle);
```

**説明**:
- セッションを終了し、リソースを解放
- 履歴ファイルが指定されている場合は保存（`TPROMPT_FLAG_NO_AUTO_SAVE`未設定時）
- 内部で作成したterseハンドルも破棄（外部から渡された場合は破棄しない）

**引数**:
- `handle`: 対象ハンドル（NULLの場合は何もしない）

### 5.2 編集セッション

#### 5.2.1 `tprompt_readline()`

```c
char *tprompt_readline(tprompt_handle_t handle, const char *prompt_override);
```

**説明**:
- 1行（複数行対応）の入力を取得
- プロンプトを動的に変更可能

**引数**:
- `handle`: 対象ハンドル
- `prompt_override`: プロンプト文字列（NULLの場合はオプションで指定したプロンプトを使用）

**戻り値**:
- 成功: 入力文字列（malloc確保、呼び出し側が`free()`する）
- EOF: `NULL`（`tprompt_get_last_error()`で`category == TPROMPT_ERROR_NONE`）
- 失敗: `NULL`（`tprompt_get_last_error()`でエラー情報取得）

**使用例**:
```c
while (running) {
    char *line = tprompt_readline(handle, get_dynamic_prompt());
    if (!line) {
        tprompt_error_info_t err = tprompt_get_last_error(handle);
        if (err.category == TPROMPT_ERROR_NONE) {
            // EOF
            break;
        }
        fprintf(stderr, "Error: %s\n", err.message);
        break;
    }

    process_command(line);
    free(line);
}
```

### 5.3 履歴機能

#### 5.3.1 `tprompt_history_add()`

```c
int tprompt_history_add(tprompt_handle_t handle, const char *entry);
```

**説明**:
- 履歴にエントリを追加
- `tprompt_readline()`は自動的に追加するため、通常は明示的な呼び出しは不要

**戻り値**:
- 成功: 0
- 失敗: -1

#### 5.3.2 `tprompt_history_load()`

```c
int tprompt_history_load(tprompt_handle_t handle, const char *file_path);
```

**説明**:
- 履歴ファイルから読み込み
- `tprompt_open()`時に自動的に呼ばれるため、通常は不要

**戻り値**:
- 成功: 0
- 失敗: -1

#### 5.3.3 `tprompt_history_save()`

```c
int tprompt_history_save(tprompt_handle_t handle, const char *file_path);
```

**説明**:
- 履歴ファイルに保存
- `tprompt_close()`時に自動的に呼ばれるため、通常は不要

**戻り値**:
- 成功: 0
- 失敗: -1

#### 5.3.4 `tprompt_history_clear()`

```c
void tprompt_history_clear(tprompt_handle_t handle);
```

**説明**:
- 履歴をクリア（ファイルは削除しない）

#### 5.3.5 `tprompt_history_set_max_size()`

```c
void tprompt_history_set_max_size(tprompt_handle_t handle, size_t max_size);
```

**説明**:
- 履歴の最大保存数を設定
- 0を指定すると無制限

### 5.4 補完機能

#### 5.4.1 `tprompt_set_completion_callback()`

```c
void tprompt_set_completion_callback(
    tprompt_handle_t handle,
    tprompt_completion_fn callback,
    void *user_data
);
```

**説明**:
- 補完コールバック関数を登録
- NULLを指定すると補完機能を無効化

#### 5.4.2 `tprompt_set_completion_prefixes()`

```c
int tprompt_set_completion_prefixes(tprompt_handle_t handle, const char *prefixes);
```

**説明**:
- 補完トリガーとなるプレフィックス文字列を設定
- 例: `"/@"` → `/` または `@` で補完開始
- NULLまたは空文字列で補完トリガーを無効化

**戻り値**:
- 成功: 0
- 失敗: -1

### 5.5 エラー処理

#### 5.5.1 `tprompt_get_last_error()`

```c
tprompt_error_info_t tprompt_get_last_error(tprompt_handle_t handle);
```

**説明**:
- 最後に発生したエラー情報を取得
- `handle`がNULLの場合はグローバルエラー情報を取得（`tprompt_open()`の失敗時など）

**戻り値**:
- エラー情報構造体

---

## 6. 使用例

### 6.1 シンプルラッパーの使用例

```c
#include <stdio.h>
#include <stdlib.h>
#include "tprompt.h"

int main(void) {
    while (1) {
        char *input = tprompt("> ");
        if (!input) {
            // EOF or error
            break;
        }

        if (strcmp(input, "exit") == 0) {
            free(input);
            break;
        }

        printf("You entered: %s\n", input);
        free(input);
    }

    return 0;
}
```

### 6.2 フレームワークAPIの使用例

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tprompt.h"

// 補完コールバック
tprompt_completion_result_t my_completion_fn(
    const char *text,
    size_t cursor_pos,
    const char *prefix_char,
    void *user_data
) {
    tprompt_completion_result_t result = {0};

    // プレフィックスが "/" の場合
    if (*prefix_char == '/') {
        // 現在のテキストから "/" 以降を抽出
        const char *cmd = strrchr(text, '/');
        if (!cmd) {
            return result;
        }
        cmd++; // "/" をスキップ

        // コマンド候補
        static const char *commands[] = {
            "/clear", "/help", "/exit", "/spark", "/special", "/start", NULL
        };

        // 候補のカウント
        size_t count = 0;
        for (size_t i = 0; commands[i]; i++) {
            const char *name = commands[i] + 1; // "/" をスキップ
            if (strncmp(name, cmd, strlen(cmd)) == 0) {
                count++;
            }
        }

        if (count == 0) {
            return result;
        }

        // 候補配列を確保
        result.candidates = malloc(sizeof(char *) * (count + 1));
        result.count = 0;

        for (size_t i = 0; commands[i]; i++) {
            const char *name = commands[i] + 1;
            if (strncmp(name, cmd, strlen(cmd)) == 0) {
                result.candidates[result.count++] = strdup(commands[i]);
            }
        }
        result.candidates[result.count] = NULL;
    }

    return result;
}

int main(void) {
    // オプション設定
    tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
    opts.history_file = "~/.myapp_history";
    opts.max_history_size = 1000;
    opts.completion_callback = my_completion_fn;
    opts.completion_prefixes = "/@";

    // 初期化
    tprompt_handle_t handle = tprompt_open(&opts);
    if (!handle) {
        tprompt_error_info_t err = tprompt_get_last_error(NULL);
        fprintf(stderr, "Error: %s\n", err.message);
        return 1;
    }

    // REPLループ
    while (1) {
        char *line = tprompt_readline(handle, "> ");
        if (!line) {
            tprompt_error_info_t err = tprompt_get_last_error(handle);
            if (err.category == TPROMPT_ERROR_NONE) {
                // EOF
                break;
            }
            fprintf(stderr, "Error: %s\n", err.message);
            break;
        }

        if (strcmp(line, "/exit") == 0) {
            free(line);
            break;
        }

        printf("You entered: %s\n", line);
        free(line);
    }

    // クリーンアップ
    tprompt_close(handle);

    return 0;
}
```

---

## 7. 設計方針

### 7.1 terseとの統合

- **内部管理**: デフォルトでは内部でterseハンドルを作成・管理
- **外部ハンドル**: `tprompt_options_t.terse_handle`で既存ハンドルを渡すことも可能
- **所有権**: 外部から渡されたterseハンドルは`tprompt_close()`時に破棄しない

**設計理由**:
- 大半のユースケースではterse-promptが完全に管理する方がシンプル
- 高度な用途（画像表示と入力処理の同一セッション）では外部ハンドルを利用

### 7.2 エラー処理

**エラー報告方式**:
- **戻り値**: NULL/-1で失敗を示す
- **詳細情報**: `tprompt_get_last_error()`で取得
- **errno**: I/Oエラーの場合はerrnoも設定

**エラー情報の保持**:
- ハンドル単位でエラー情報を保持
- `tprompt_open()`失敗時はグローバルエラー情報
- 成功時は前回のエラー情報をクリア

### 7.3 メモリ管理

**基本原則**:
- **入力文字列**: 呼び出し側が`free()`で解放
- **補完候補**: `tprompt_free_completion_result()`で解放
- **内部バッファ**: ハンドル破棄時に自動解放

**動的拡張**:
- 入力バッファは初期サイズ小（例: 256バイト）
- 必要に応じて自動拡張（最大: `max_input_size`）

### 7.4 UTF-8対応

**文字エンコーディング**:
- **内部表現**: UTF-8バイト列
- **文字幅計算**: `terse_event_t.data.ch.width`を利用
- **カーソル位置**: バイトオフセットで管理（内部で文字境界を考慮）

**制限**:
- 初期バージョンは複雑なグラフィームクラスタに対する完全サポートなし
- 削除時に不完全になる可能性あり（将来改善）

### 7.5 スレッドセーフティ

**初期バージョン**:
- シングルスレッド専用
- 同一ハンドルへの複数スレッドからのアクセスは未定義動作

**将来**:
- スレッドごとのハンドルは問題なく使用可能
- グローバル状態は最小限（エラー情報のみ、TLSで対応検討）

### 7.6 キーバインディング

**初期バージョン**:
- Emacs風キーバインド固定

**拡張設計**:
```c
typedef enum tprompt_keymap {
    TPROMPT_KEYMAP_EMACS = 0,
    TPROMPT_KEYMAP_VI,
    TPROMPT_KEYMAP_CUSTOM
} tprompt_keymap_t;

// 将来追加予定
int tprompt_set_keymap(tprompt_handle_t handle, tprompt_keymap_t keymap);
```

---

## 8. API一覧

### シンプルラッパー
| 関数 | 説明 |
|------|------|
| `tprompt()` | 1行入力取得 |

### 初期化・クリーンアップ
| 関数 | 説明 |
|------|------|
| `tprompt_open()` | セッション開始 |
| `tprompt_close()` | セッション終了 |

### 編集
| 関数 | 説明 |
|------|------|
| `tprompt_readline()` | 1行入力取得（プロンプト指定可） |

### 履歴
| 関数 | 説明 |
|------|------|
| `tprompt_history_add()` | 履歴追加 |
| `tprompt_history_load()` | 履歴読み込み |
| `tprompt_history_save()` | 履歴保存 |
| `tprompt_history_clear()` | 履歴クリア |
| `tprompt_history_set_max_size()` | 最大サイズ設定 |

### 補完
| 関数 | 説明 |
|------|------|
| `tprompt_set_completion_callback()` | 補完コールバック登録 |
| `tprompt_set_completion_prefixes()` | 補完トリガー設定 |
| `tprompt_free_completion_result()` | 補完候補解放 |

### エラー処理
| 関数 | 説明 |
|------|------|
| `tprompt_get_last_error()` | 最後のエラー情報取得 |

---

## 9. 変更履歴

| 日付 | バージョン | 変更内容 |
|------|-----------|---------|
| 2025-11-02 | 0.1 | 初版作成 |
