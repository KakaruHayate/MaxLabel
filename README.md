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

### 开发用的两个开关

- `--screenshot <file>`：把窗口渲染成 PNG 后退出。**界面好不好看，看图片比看描述准**——主题就是这么调出来的。
- `--spectrum`：以频谱模式启动。

## 命令行

```bash
maxlabel_cli scan <dir>              # 列出目录里的段落、来源与 PFML 是否合法
maxlabel_cli show <dir> <id>         # 打印某段的 PFML
maxlabel_cli validate <file.pfml>    # 只做语法校验
maxlabel_cli set <dir> <id> [file]   # 写入 <id>.pfml（无 file 时读 stdin）
maxlabel_cli langs <file> [-l zh]    # 按语言切分转录文本，打印得到的 PFML
maxlabel_cli phoneme <符号> [-l zh,en] [--vocab <file>]
                                     # 查一个音素在模型词表里能不能解析
maxlabel_cli candidates <文本> --g2p <config.json> [--dicts <dir>] [-l zh,en]
                                     # 列出管线给出的候选发音
```

`scan` 会把 PFML 不合法的段落标出来并以非零码退出 —— 因为那些样本对齐器会静默跳过。

## 词表与 G2P 配置是可选的

两者都**不配置也能用**，只是退化：

| 配置 | 有 | 没有 |
|---|---|---|
| `--vocab <符号表>` | 发音弹窗逐个音素校验，不在表里的打红色波浪线 | 不校验，状态栏明说"phonemes unchecked" |
| `--g2p <config.json> --dicts <dir>` | 发音弹窗列出词典给出的候选，点选即填 | 手打音素 |

符号表是**一行一个符号**（`#` 开头是注释）。它本来在模型的 GGUF 里，而 MaxLabel 按设计不含 ggml，
所以取导出的列表而不是长一个 GGUF reader。

> **已知缺口**：tifa.cpp 的 `tifa_ggml_cli inspect` 只打印词表大小，**不导出符号列表**。
> 所以目前要拿到这份列表还得另找办法。给 `inspect` 加一个导出选项是个小改动，值得提给上游。

## 语言切分

对齐器按语言把片段路由给对应的转换器，**语言错了音素就错了，而且是静默错的**。
所以切分是 MaxLabel 存在的理由之一。

切分流程**照搬 GPT-SoVITS 的文字前端**（`split-lang` + `LangSegmenter.getTexts`）：

| 步骤 | 规则 |
|---|---|
| 预切分 | 汉字与假名归为**同一类**（`ZH_JA`），韩文、数字、标点、其它各自成类 |
| 判日语 | 一个 `ZH_JA` 段里**只要出现假名，整段判 `ja`**（中文不写假名） |
| 判韩语 | 谚文 → `ko` |
| 短英文纠偏 | `full_en`：整段是 ASCII 可见字符且含字母 → `en` |
| 夹在别的语言里的假名/谚文 | `split_jako` 把它们拆出来单独标记 |
| 未知段含汉字 | `full_cjk`：抽出汉字 → 判 `zh` |
| 数字 | 按前后邻居的语言解析（标点、长度都参与判断），而不是一刀切 |
| 残留未知 | 前一个的语言 → 后一个的语言 → `zh` |

**效果**（`maxlabel_cli langs` 可直接看）：

```
今天天气不错 I love you   →  zh | en
東京へ行く                →  ja          ← 整段，因为出现了假名
東京                      →  ja          ← 纯汉字，由检测模型给出答案
衬衫的价格是9.15便士       →  zh          ← 数字被解析并合并回中文
안녕하세요 world           →  ko | en
```

## 模型与数据文件

| 文件 | 大小 | 许可 | 来源 |
|---|---|---|---|
| `models/budoux/ja.json` | 20 KB | Apache-2.0 | 随仓库提交 |
| `models/budoux/zh-hans.json` | 64 KB | Apache-2.0 | 随仓库提交 |
| `models/lid.176.bin`（或 `.ftz`） | 125 MB / 916 KB | **CC BY-SA 3.0** | 由 CI 下载，打进发布包 |

- **BudouX** 是纯 JSON 的线性模型（13 张特征表），负责把中日文段再细分成词，
  好让检测器看到的是词而不是整段。移植后与原版 Python **逐例一致**，测试里钉住了参考输出。
- **fastText lid.176** 是唯一真正"是模型"的部分，也是唯一能对**纯汉字段**和**非英语拉丁段**给出答案的东西。
  注意：**代码 MIT，权重 CC BY-SA 3.0**。`split-lang` 写死用 `full`（125 MB），
  CI 用 `lite`（916 KB）跑得快一些，两者是同一个训练结果的不同精度。
- 运行时按顺序找：`--models <目录>` > 二进制旁边的 `models/` > 编译进去的源码目录。

> **语言表是可扩展的**（`include/maxlabel/languages.h`）：语言、显示名、检测器 id 的映射、快捷键都在表里，
> GUI 的语言按钮也是从表生成的。加一门语言是加一条数据，不是改分支。
## 当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| M-1 | tifa.cpp：拆出 ggml-free 的 G2P 库、公开 API、补全 PFML 解析、实现 `to_pfml` | ✅ 已完成（tifa.cpp `feat/g2p-public-api`，已合并） |
| M0 | 数据层：目录扫描 / 导入 / 导出 / 校验闸门 + CLI | ✅ 已完成 |
| M1a | 语言切分与区间模型（字形定语言、汉字不猜、序列化为 `<scope>`） | ✅ 已完成 |
| M1b | Qt6 GUI：**文本优先**编辑 + 语言底色 + 歧义高亮 + 手动指定 + PFML 预览 | ✅ 已完成 |
| M1c | 手动分词：`<word>` 边界（选中按 `W`），实线下划线显示 | ✅ 已完成 |
| M3a | 覆盖模型：指定发音 / 直写音素 / 插入音素，序列化为 `<word phonemes>` 与 `<phoneme>` | ✅ 已完成 |
| M3b | 音素表与校验（`--vocab`，不在表里即标记） | ✅ 已完成 |
| M3c | GUI：发音弹窗（候选 / 手打）、插入音素、非词汇音面板、非法音素波浪线 | ✅ 已完成 |
| M2 | 音频面板：解码（wav/flac/mp3）、波形 / 频谱、选区播放、text-first 配对 | ✅ 已完成 |

### GUI 现在能做什么

`MaxLabel <文件夹> [--vocab <符号表>] [--g2p <config.json> --dicts <目录>] [--models <目录>]`

**外观**参考 [R3MOE](https://github.com/KakaruHayate/R3MOE) 的 mouth baker：
VS Code 暗色中性色 + Material 粉 `#E91E63` 强调色，左侧控制栏、全大写分组标题、扁平按钮。
调色板在 `src/ui/theme.qss`；图标是 `src/ui/icons/*.svg`（单色，运行时替换颜色后渲染，
所以同一个文件能同时服务灰色常态和强调色上的白色）。版式参考 Aegisub：**音频在上，图标工具条夹在中间，文本在下**。

**界面语言跟随系统**，中文系统上就是中文。英文是源语言，代码里的字符串就是它显示的内容。
`--lang en` / `--lang zh_CN` 可覆盖（测试两种语言就是从一台机器上做的）。
翻译是标准的 Qt `.ts`/`.qm` 流程：`src/ui/i18n/maxlabel_zh_CN.ts` 由 `lupdate` 提取、
`lrelease` 编译进二进制。

- **音频区**（没有音频的段落会整个收起）：整段音频常驻，点击定位、拖拽选区。
  `≋` 切换**波形 / 频谱**。频谱是时频图 —— 时间沿 X、频率沿 Y，所以它要的是宽度，
  这也是音频区做成通栏而不是右侧窄条的原因。
- **图标工具条**：`▶` 播放（有选区就播选区）、`■` 停止、`«`/`»` 前后 0.5 秒、`≋` 波形/频谱、`⌫` 清除选区。
- **中间是文本**，不是 PFML。语言区间按底色显示，**未定的区间用红色波浪线**而不是颜色 ——
  颜色读起来像"已经有答案了"，而这里还没有。选中一段按 `1`/`2`/`3`/`4` 指定 zh/ja/en/ko。
- 选中一段按 `W` 固定成一个词（`<word>`），**实线下划线**。
- 选中一段按 `P` 指定发音（**点线下划线**）；光标处按 `I` 插入音素；
  「Non-lexical ▾」一键插入 `AP`/`SP`/`sil` 等。
- 指定发音的弹窗里：配了 G2P 就**列出词典的候选**，点一下即填；没配就手打。
  配了词表就**逐个音素校验**，不在表里的名字会被点出来 —— 而不是等到对齐时才发现。
- **下方是只读的 PFML 预览**，也就是实际会写出去的内容。
- `Ctrl+S` 保存，`Z`/`X` 上一条/下一条，**切换前自动提交**。`R` 重新切分。
  `Ctrl+Space` 从任何地方播放；音频区获得焦点后 `Space`/`B` 播放、`H` 停止、`Q`/`W`/`←`/`→` 微调
  （这样 `Space` 在编辑器里仍然是空格）。

> 手动标过语言的段落，**改动文本会重新推导语言区间**（旧偏移已经不对应新文本了）。
> 状态栏在手动标记存在时会提示这一点，不是静默丢弃。

> **一个 PFML 本身的限制**：`<word>` 只能嵌在 `<scope>` 里，不能反过来。
> 所以跨语言边界的词**无法表达** —— 这时工具会在预览和状态栏报告，把它留作普通文本，
> 而不是悄悄拆开或丢掉。

## 设计要点

- **不自己实现 PFML 语法**：一律操作 `G2PWord`/`G2PReading`/`G2PPath`/`G2PGroup`
  对象模型，序列化交给 `tifa_ggml::to_pfml()`，往返无损。
- **导出前必须校验**：对齐器对无效 PFML 没有回退，所以 `save()` 先 `validate_pfml()`，
  不合法就拒绝写入。
- **任何修改立即落盘**：不留 pending 状态，切段不会丢数据。
- **不做下游**：TIFA 输出侧的转换、切片修正都不在本项目范围内。

## 许可

MPL-2.0，与 tifa.cpp 一致。
