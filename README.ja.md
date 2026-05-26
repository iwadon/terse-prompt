# terse-prompt

CLIアプリケーション向けの軽量でモダンなC言語製マルチライン入力ライブラリ。GNU readlineやlinenoiseのような機能を、モダンなターミナルインターフェース向けに設計しました。

## 特徴

- **マルチライン編集**: 自動折り返し機能を備えた完全なマルチライン入力対応
- **カスタマイズ可能な継続行プロンプト**: 継続行の表示方法を設定可能
- **UTF-8対応**: 国際化テキストのネイティブUTF-8処理
- **コマンド履歴**: ファイル保存とLRU削除機能付きの永続的な履歴管理
- **タブ補完**: トリガーベースの補完システムとインクリメンタルフィルタリング
- **カスタムキーバインド**: 入力動作をカスタマイズできる柔軟なキーバインドシステム
- **ステータスライン**: アプリケーション固有情報を表示できるカスタマイズ可能なステータスライン
- **効率的な描画**: ちらつきのない差分レンダリング
- **2種類のAPIスタイル**: 基本的な用途向けのシンプルラッパーと、高度なカスタマイズ向けのフレームワークAPI
- **外部依存なし**: 同梱の`terse`ターミナル制御ライブラリのみを使用

## クイックスタート

### ビルド

```bash
# リポジトリをクローン
git clone https://github.com/iwadon/terse-prompt.git
cd terse-prompt

# 設定とビルド（依存ライブラリはCMake FetchContentにより自動取得されます）
cmake -B build -G Ninja
cmake --build build

# テストの実行
ctest --test-dir build
```

### 基本的な使い方

```c
#include <tprompt.h>
#include <stdio.h>

int main(void) {
    // シンプルな1行: readline()と同様
    char *input = tprompt(">>> ", NULL);
    if (input) {
        printf("You entered: %s\n", input);
        free(input);
    }
    return 0;
}
```

### マルチラインと履歴機能

```c
#include <tprompt.h>
#include <stdio.h>

int main(void) {
    // オプションの設定
    tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
    opts.history_file = ".my_history";
    opts.max_history_size = 1000;
    opts.flags = TPROMPT_FLAG_MULTILINE;

    // セッションを開く
    tprompt_handle_t *tp = tprompt_open(&opts);
    if (!tp) {
        fprintf(stderr, "Failed to open tprompt\n");
        return 1;
    }

    // ループで入力を読み取る
    while (1) {
        char *input = tprompt_readline(tp, ">>> ");
        if (!input) break;  // EOFまたはエラー

        if (*input) {
            tprompt_add_history(tp, input);
            printf("You entered:\n%s\n", input);
        }

        free(input);
    }

    tprompt_close(tp);
    return 0;
}
```

## API概要

### シンプルAPI

基本的な用途では、1つの関数だけで使えます：

```c
char *tprompt(const char *prompt, tprompt_options_t *options);
```

ユーザー入力を含むmallocされた文字列を返します。使用後はfreeしてください。

### フレームワークAPI

高度な機能（補完、ステータスライン、バリデーション）向け：

```c
// セッション管理
tprompt_handle_t *tprompt_open(tprompt_options_t *options);
void tprompt_close(tprompt_handle_t *tp);

// 入力の読み取り
char *tprompt_readline(tprompt_handle_t *tp, const char *prompt);

// 履歴管理
void tprompt_add_history(tprompt_handle_t *tp, const char *line);
void tprompt_clear_history(tprompt_handle_t *tp);

// 補完（オプションでコールバックを設定）
typedef char **(*tprompt_completion_fn)(
    void *userdata,
    const char *input,
    size_t cursor_position,
    size_t *count_out
);

// ステータスライン（tprompt_set_status_line_callback経由で設定）
typedef char *(*tprompt_status_line_fn)(
    void *userdata,
    const char *input,
    size_t cursor_position
);

// カスタムキーバインド
void tprompt_set_keybindings(
    tprompt_handle_t *tp,
    tprompt_keybinding_t *bindings,
    size_t count
);
```

## 高度な機能

### 継続行プロンプトのカスタマイズ

マルチラインモードでの継続行の表示方法をカスタマイズできます：

```c
tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
opts.prompt = ">>> ";
opts.continuation_prompt = "... ";  // Pythonスタイルの継続行

// 指定しない場合はデフォルトの "| " が使用されます
```

継続行プロンプトは初期プロンプトの幅に自動調整されます：
- **短い場合**: 右詰めでスペースを追加し、テキストの開始位置を揃えます
- **長い場合**: 警告を出した上で切り詰められ、表示の乱れを防ぎます

### タブ補完

```c
char **my_completion_fn(void *userdata, const char *input,
                        size_t cursor_pos, size_t *count_out) {
    // 入力に基づいて補完候補を生成
    static char *completions[] = { "foo", "bar", "baz" };
    *count_out = 3;
    return completions;
}

tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
opts.completion_fn = my_completion_fn;
opts.completion_triggers = "/";  // '/'でトリガー
```

### カスタムキーバインド

```c
// Enterキーの動作をカスタマイズ
tprompt_keybinding_t bindings[] = {
    // Enterで確定、Shift+Enterで改行挿入
    TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
    TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, TERSE_MOD_SHIFT, TPROMPT_ACTION_INSERT_NEWLINE),
    // Ctrl+Jでも改行挿入
    TPROMPT_BIND_CHAR('j', TERSE_MOD_CTRL, TPROMPT_ACTION_INSERT_NEWLINE),
};

tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
opts.custom_keybindings = bindings;
opts.keybinding_count = 3;
```

バリデーションをスキップする「強制確定」キーが必要な場合は、
`TPROMPT_ACTION_CONFIRM_WITHOUT_VALIDATION` を別途（例: Alt+Enter に）バインドしてください。
デフォルトでは有効化されません。

### ステータスライン

```c
char *my_status_fn(void *userdata, const char *input, size_t cursor_pos) {
    static char buf[128];
    snprintf(buf, sizeof(buf), "Position: %zu | Length: %zu",
             cursor_pos, strlen(input));
    return buf;
}

tprompt_handle_t *tp = tprompt_open(&opts);
tprompt_set_status_line_callback(tp, my_status_fn, NULL);
```

### 入力バリデーション

```c
typedef enum {
    TPROMPT_VALIDATION_VALID,
    TPROMPT_VALIDATION_INCOMPLETE,
    TPROMPT_VALIDATION_INVALID
} tprompt_validation_result_t;

tprompt_validation_result_t my_validator(void *userdata, const char *input) {
    // 入力が有効かチェック
    if (needs_more_input(input))
        return TPROMPT_VALIDATION_INCOMPLETE;
    if (is_invalid(input))
        return TPROMPT_VALIDATION_INVALID;
    return TPROMPT_VALIDATION_VALID;
}

tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
opts.validation_fn = my_validator;
```

## キーバインド

デフォルトのキーバインド：

- `Enter`: 入力を確定
- `Shift+Enter` または `Ctrl+Enter`: 送信せずに改行を挿入

バリデーションを飛ばすショートカットが必要な場合は、カスタムキーバインドで
`TPROMPT_ACTION_CONFIRM_WITHOUT_VALIDATION`（例: Alt+Enter）を追加してください
（デフォルトでは無効）。

- **ナビゲーション**:
  - 矢印キー: カーソル移動
  - `Home`/`End`: 行の先頭/末尾へ移動（2回押すと論理行の先頭/末尾）
  - `Ctrl+A`/`Ctrl+E`: 行の先頭/末尾へ移動

- **編集**:
  - `Backspace`/`Delete`: 文字削除
  - `Ctrl+U`: 行頭まで削除
  - `Ctrl+K`: 行末まで削除

- **履歴**:
  - `Up`/`Down`: 履歴をナビゲート

- **補完**:
  - トリガー文字を入力（例: `/`）
  - 矢印キー: 候補をナビゲート
  - `Tab`: 候補を選択
  - `Esc`: 補完をキャンセル

## プラットフォーム別のビルド

### Unix系（Linux、macOS）

```bash
cmake -B build -G Ninja
cmake --build build
```

### Windows（MSVC）

```powershell
# Visual Studio 2019以降が必要
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

## ドキュメント

- `docs/requirements.md`: 機能要件と仕様
- `docs/api-design.md`: APIの詳細設計と例
- `include/tprompt.h`: 完全なAPIリファレンス
- `CLAUDE.md`: 開発者向けガイド

## サンプル

`examples/`ディレクトリを参照してください：
- `simple_demo.c`: シンプルAPIの基本的な使い方
- `advanced_demo.c`: 補完、ステータスライン、カスタムキーバインドの例

ビルドと実行：
```bash
cmake --build build
./build/examples/simple_demo
./build/examples/advanced_demo
```

## ライセンス

MIT-0（MIT No Attribution） - 詳細はLICENSEファイルを参照してください

## 貢献

貢献を歓迎します！以下をお願いします：
1. `CLAUDE.md`で開発ガイドラインを確認
2. すべてのテストが通ることを確認：`ctest --test-dir build`
3. コミットメッセージはConventional Commitsに従う
4. APIの変更時はドキュメントも更新

## クレジット

[terse](https://github.com/iwadon/terse)（最小限のターミナル制御ライブラリ）をベースに構築されています。
