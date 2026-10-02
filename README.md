# MaxLabel

PFML（Pronunciation Flow Markup Language）的编辑与校对工具 —— **TIFA 之前的那一步**。

TIFA 可以直接吃文本，但它需要一个"哪里该断词、哪段是什么语言、哪个字读什么"的载体，
这个载体就是 PFML。手写 PFML 既难又容易出错（**无效 PFML 的样本会被对齐器直接跳过、
没有回退**），而 MinLabel 那套"音频配 .lab"的流程在文本优先的新链路里已经没有位置。
MaxLabel 填的就是这个空。

```
AudioSlicer ──► (文本 / .lab+json / .pfml)
                        │
                        ├──► MaxLabel ──► song.pfml          ← 本项目
                        │
                        └──► TIFA ──► TextGrid + diagnosis.json
```

- **输入**：`wav` +（可选）`.pfml` / `.txt` / `.lab` / `.json`；只有 wav 时从零搭建
- **输出**：只有 `.pfml` 一种

## 项目就是一个文件夹

不需要工程文件、没有清单要同步：同目录下同名文件即属同一条。

```
song.wav
song.pfml      ← 内容真源，本工具唯一会写的文件
song.txt       ← 纯文本转录（可选输入）
song.lab       ← 旧 MinLabel 音节行（可选输入）
song.json      ← 旧 MinLabel 工程文件（可选输入，isCheck 映射为校对状态）
```

读取优先级与对齐器一致：**`.pfml` > `.txt` > `.lab`**；`.json` 里 LyricFA 写入的
`raw_text` 是匹配到的歌词原文，优先级排在 `.txt` 之前。

## 构建

依赖 [tifa.cpp](https://github.com/KakaruHayate/tifa.cpp) 的 `tifa_ggml_g2p` ——
G2P 管线与 PFML 解析/序列化，**不含 ggml、不需要模型**。
有同级 checkout 时直接用，否则 CMake 自动拉取。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Qt6 是**可选**的：没装 Qt 时只构建核心库与 CLI，数据层不依赖 Qt。
装了 Qt 时用 `-DCMAKE_PREFIX_PATH=<Qt kit 路径>` 让它找到。

```bash
cmake -S . -B build -DMAXLABEL_BUILD_UI=OFF    # 只要 CLI
```

> `MAXLABEL_BUILD_UI=ON`（默认）表示"就是要构建 GUI"，找不到 Qt 会直接报错，
> 而不是静默降级 —— 否则 CI 可能在从未编译过 GUI 的情况下通过。

## 下载

预编译包见 [Releases](https://github.com/KakaruHayate/MaxLabel/releases)：

| 包 | 内容 |
|---|---|
| `MaxLabel-windows-x64.zip` | GUI + CLI，已带 Qt 运行时（`windeployqt`），解压即用 |
| `MaxLabel-macos-arm64.tar.gz` | GUI + CLI，已带 Qt 运行时（`macdeployqt`）；未签名，首次需右键「打开」 |
| `MaxLabel-linux-x64.tar.gz` | GUI + CLI，**未捆绑 Qt**，需要系统里有 Qt6 运行时（如 `qt6-base-dev`）；CLI 部分不依赖 Qt，可直接用 |

## 命令行

```bash
maxlabel_cli scan <dir>              # 列出目录里的段落、来源与 PFML 是否合法
maxlabel_cli show <dir> <id>         # 打印某段的 PFML
maxlabel_cli validate <file.pfml>    # 只做语法校验
maxlabel_cli set <dir> <id> [file]   # 写入 <id>.pfml（无 file 时读 stdin）
maxlabel_cli langs <file> [-l zh]    # 按语言切分转录文本，打印得到的 PFML
```

`scan` 会把 PFML 不合法的段落标出来并以非零码退出 —— 因为那些样本对齐器会静默跳过。

## 语言切分

对齐器按语言把片段路由给对应的转换器，**语言错了音素就错了，而且是静默错的**。
所以切分是 MaxLabel 存在的理由之一。

三种情况由字形直接定：

| 字形 | 语言 |
|---|---|
| 假名 | `ja`（中文不写假名） |
| 谚文 | `ko` |
| 拉丁 | `en` |

**汉字是字形定不了的那一种**：`東京` 是 dōngjīng 还是 tōkyō，`唔该` 是普通话还是粤语，
写出来一模一样。所以汉字片段**不猜**：

- 给了 `-l`（项目默认语言）→ 用默认语言；
- 没给 → 报为**未定**，在 PFML 里保留为裸文本，由人来定。

宁可留着不猜，也不猜错 —— 猜错的话对齐器会用错语言的转换器跑出**看着合理的错误结果**。
`langs` 命令在存在未定片段时以非零码退出。

## 当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| M-1 | tifa.cpp：拆出 ggml-free 的 G2P 库、公开 API、补全 PFML 解析、实现 `to_pfml` | ✅ 已完成（tifa.cpp `feat/g2p-public-api`，已合并） |
| M0 | 数据层：目录扫描 / 导入 / 导出 / 校验闸门 + CLI | ✅ 已完成 |
| M1a | 语言切分与区间模型（字形定语言、汉字不猜、序列化为 `<scope>`） | ✅ 已完成 |
| M1b | Qt6 GUI：**文本优先**编辑 + 语言底色 + 歧义高亮 + 手动指定 + PFML 预览 | 🚧 可用，待做分词编辑 |
| M2 | 音频面板（波形/频谱、选区播放、text-first 配对） | ⬜ 未开始 |
| M3 | 发音决策（候选弹窗、锚定、插入音素、非词汇音面板） | ⬜ 未开始 |

### GUI 现在能做什么

`MaxLabel <文件夹>` 或工具栏「Open Folder…」打开一个项目：

- **中间是文本**，不是 PFML。语言区间按底色显示，**未定的区间用红色波浪线**而不是颜色 ——
  颜色读起来像"已经有答案了"，而这里还没有。选中一段按 `1`/`2`/`3`/`4` 指定 zh/ja/en/ko。
- **下方是只读的 PFML 预览**，也就是实际会写出去的内容。
- `Ctrl+S` 保存，`Z`/`X` 上一条/下一条，**切换前自动提交**。
- `R` 重新切分（会丢掉手动标记）。
- 保存前校验：不合法的片段不会被写入，并告诉你原因。

> 手动标过语言的段落，**改动文本会重新推导语言区间**（旧偏移已经不对应新文本了）。
> 状态栏在手动标记存在时会提示这一点，不是静默丢弃。

## 设计要点

- **不自己实现 PFML 语法**：一律操作 `G2PWord`/`G2PReading`/`G2PPath`/`G2PGroup`
  对象模型，序列化交给 `tifa_ggml::to_pfml()`，往返无损。
- **导出前必须校验**：对齐器对无效 PFML 没有回退，所以 `save()` 先 `validate_pfml()`，
  不合法就拒绝写入。
- **任何修改立即落盘**：不留 pending 状态，切段不会丢数据。
- **不做下游**：TIFA 输出侧的转换、切片修正都不在本项目范围内。

## 许可

MPL-2.0，与 tifa.cpp 一致。
