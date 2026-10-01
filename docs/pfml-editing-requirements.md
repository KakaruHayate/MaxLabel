# PFML 编辑需求 —— 立项讨论稿

日期：2026-10-01
状态：讨论中，未定案

---

## 0. 结论先行

三句话：

1. **不建议自研"复文本编辑器"。** 这个需求本质是"低频的例外修正"，不是"内容创作"。为一个修正动作造一个完整编辑器，投入产出比确实很差——你的"鸡肋感"是对的，而且它应该被当成设计约束，而不是被当成要克服的困难。
2. **现有软件里没有任何 PFML 编辑器，也不建议往 vLabeler 里塞。** vLabeler 是很好的可复用宿主，但它解决的是流程的**另一段**（对齐之后的、带时间轴的音频标注）；PFML 是**对齐之前的、纯文本的**。数据模型对不上。
3. **真正值得做的东西，核心不是"编辑界面"，而是"g2pflow 的可视化前端 + 校验器"。** 如果要做，形态是一个百行级的 CLI 包装（`check` / `preview` / `candidates`），可选再叠一个编辑器内联提示层。成本以"天"计，而不是"周"。

甚至存在一种可能：**先什么都不做**，先花两天把"到底有多少样本非改 PFML 不可"量化出来，再决定要不要做。

---

## 1. 事实基线（先把事实钉死）

### 1.1 数据流

```
纯文本 .txt/.lab ──┐
                   ├──> TIFA (强制对齐) ──> TextGrid (texts/words/phones 三层)
PFML .pfml ────────┘         │
                             └──> statistics/scores.json, diagnosis.json
```

关键机制（来自 TIFA README）：

- **转录文件查找优先级：`.pfml` > `.txt` > `.lab`**，同 basename。
- **只有 `.pfml` 会被当作标记语言解析**；`.txt` / `.lab` 一律当纯文本。
- **无效 PFML 的样本会被直接跳过，没有回退。** ← 这条后面会反复用到，是本需求里最大的坑。
- 语言：`-l` 默认语言，`-L` 追加语言（如 `-l zh -L en`）。`-l ja` 会去掉 `ja/` 前缀。
- **发音候选打分默认开启**：对齐前会为每个词联合挑选最优发音，`scores.json` 只记录有多个候选的词。
- `diagnosis.json` 会**把可疑样本排在前面**，附带 agreement / confidence / determinacy / monotonicity 四个自洽性指标。

### 1.2 g2pflow / PFML

- g2pflow 是**纯 Python 库**：README 明确说它没有 CLI、没有配置加载器、没有 GUI。任何"前端"都必须自己拉进程或包一层服务。
- PFML 1.0 是 **XML fragment**（无根元素），6 个元素：

| 元素 | 作用 | 属性 |
|---|---|---|
| `scope` | 语言作用域，不固定词边界 | `language` |
| `word` | 一个固定词或完整结果 | `text`, `language`, `script`, `phonemes`, `language-kind` |
| `reading` | 一个读音，含多个候选 path | 无 |
| `path` | 一条有序发音路径 | 无 |
| `group` | 一个音节/读音单元 | `script`, `phonemes` |
| `phoneme` | 一个最终音素 | `symbol`, `language` |

- 词分两类：**automatic**（给文本，走 G2P）和 **direct**（音素全给，绕过 G2P）。两者不能混。
- 语言 ID 是**不透明的应用自定义字符串**，不做 trim / 大小写折叠 / 规范化；显式 scope 只匹配注册过的确切 ID，**不匹配就报错，没有 fallthrough**。
- `<` 和 `&` 必须转义为 `&lt;` / `&amp;`（或用 CDATA）。
- `parse_pfml()` 本身就是**完整校验器**，失败抛 `PFMLError`；`convert_pfml()` 执行；`to_pfml()` 无损序列化。
- g2pflow 有**插件体系**（`@preprocessor` / `@converter`），可注册自定义词典和转换器。

一个真实例子（日本语「猫」）：

```xml
<word text="猫" language="ja"><reading><path><group script="ne"><phoneme symbol="n"/><phoneme symbol="e"/></group><group script="ko"><phoneme symbol="k"/><phoneme symbol="o"/></group></path></reading></word>
```

多语言例子：

```xml
<scope language="zh">我住在<word>重庆</word>，喜欢<scope language="en">New York</scope>。</scope>
```

### 1.3 被取代的 MinLabel `.lab`

旧格式是**扁平的一行、空格分隔的音节**，例如：

```
gan shou ting zai wo fa duan de zhi jian
```

没有语言、没有词边界、没有候选、没有音素层——音素层是后面 MFA 对齐产出 TextGrid 时才有。PFML 相对它的增量，正是 scope / word / reading / path / group / phoneme 这棵树。

---

## 2. 关键重构：这是"修正"，不是"编辑"

这是整份讨论里最重要的一段，它直接决定要不要做、做多大。

**你列的三个难点（多语言混排、多音字、日语汉字读音），TIFA + g2pflow 已经自动解决了大部分：**

- 多音字 → TIFA 的**候选打分**会结合音频挑最合适的读音；
- 多语言 → `-l` / `-L` + 默认 G2P 管线已覆盖中（普通话/粤语）、日、英；
- 日语汉字 → `japanese-mecab` 转换器负责分词与读音。

所以 PFML 的定位是**逃生舱**：只在自动结果错了的时候，用来覆盖。这带来两个推论：

**推论 A：需求量天然很低，而且低得有道理。** 不是"我们偷懒"，而是"自动管线本来就该扛住 95%"。"很久才编辑几次"是这类格式的健康状态，不是缺陷。

**推论 B：很多"我要改 PFML"的诉求，其实不该在 PFML 层解决。**

- 如果是**系统性**错误（一批多音字总是错、某个领域词总读错）→ 正确做法是给 g2pflow 写**词典/转换器插件**或改 `g2p.yaml`，一次解决一批。手改标记是最差的解法。
- 如果是**单样本、跟音频相关**的修正（这个词在这段音频里读错了）→ 那属于对齐**之后**的工作，应该在下游 TextGrid 里改（Praat / vLabeler），而不是回到 PFML 改。

也就是说：**PFML 手工编辑可能大部分是可以被绕开的。** 在决定造工具之前，这件事必须先验证。

---

## 3. 四个编辑需求拆解

你提的四点，难度和"工具价值"并不一样：

| 需求 | 手写难度 | 工具价值 | 更优解法 |
|---|---|---|---|
| 1. 多语言 scope | 极低（包一层标签） | **低** | 优先用 TIFA `-l`/`-L`，多数情况根本不用 PFML |
| 2. 手动分词 | 低（`<word>` 包一下） | **中** | 难点不在写，而在**知道默认切错了**——需要 preview |
| 3. 指定发音 | 高（要先拿到候选） | **高** | 需要候选列表 / 选择器 |
| 4. 直写音素 | 高（要模型词表 + 校验） | **高但受众极窄** | 需要音素补全 + 校验 |

**关键观察：只有 3 和 4 真正需要工具，而它们依赖的是同一个能力——跑 g2pflow 并把结果展示出来。**

于是工具的本质被重新定义：**不是"编辑界面"，而是"g2pflow 的前端 + 校验器"。** 界面只是这个能力的副产品。这大幅降低了工作量预期。

---

## 4. 真正的痛点排序（决定工具该做什么）

按"不解决会怎样"排序：

1. **静默失败（最高）**：无效 PFML 的样本会被 TIFA **直接跳过、没有回退**。手写时漏一个 `&amp;`、属性名拼错、层级放错，样本就悄无声息地消失了。→ **校验器价值最高。**
2. **看不见 G2P 结果（高）**：不知道默认会怎么分词、怎么注音，就无从判断"要不要干预"。→ **preview 价值高。**
3. **不知道音素表（中）**：直写音素需要模型的音素词表，否则纯靠猜。→ **补全有价值。**
4. **编辑手感差（最低）**：语法高亮、折叠、格式化……听着美好，其实是最不重要的一环。**而"复文本编辑器"这个诉求，主要就是在解决第 4 条。**

这就是为什么"造一个完备的复文本编辑器"是错的发力方向——它把最大的力气花在了最不痛的地方。

---

## 5. 现有软件盘点

回答你"有没有现成软件可以插件化复用"这个问题：

### 5.1 领域内

- **没有任何 PFML 编辑器存在。** 全 GitHub 搜 `PFML` / `g2pflow` 只有 g2pflow 本身；openvpi 全部 19 个仓库里也没有。这是**全新领域**，没有可直接集成的东西。
- **vLabeler**（`sdercolin/vlabeler`）：**领域契合度最高的宿主**，Kotlin + Compose Multiplatform，有成熟的 **JS 插件系统**（`plugin.json` + JS 脚本），有"格式无关 labeler 抽象"，社区已有多个自定义 labeler（如 `vlabeler-phonemeflow`）。
  **但是**：它的数据模型是 **audio-first / entry-based（条目 + 时间）**，而 PFML 是 **text-first、无时间轴、带候选树**。硬套要么把树压扁成条目，要么给 vLabeler 加一套它本不擅长的东西。**结论：不适合承载 PFML；但它是对齐后 TextGrid 修正的最佳现成工具，应该保留在流程里。**
- **Praat / ELAN / Label Studio / Aegisub / WaveSurfer**：全部是 audio-first、时间对齐导向，与 PFML 错配。Audacity label 更是只有 `start\tend\ttext`。

### 5.2 通用宿主

| 宿主 | 插件形态 | 内联标注能力 | 能否调本地进程 | 跨平台 | 判断 |
|---|---|---|---|---|---|
| **VS Code** | TS/JS 扩展，Custom Editor + Webview | **齐全**：decoration / inlay hint / CodeLens / diagnostics / LSP | 能（`child_process`） | 能 | **最佳通用宿主**，成本中等；受众是问题 |
| Obsidian | TS 插件，CodeMirror 6 | 能（Decoration） | 仅桌面 | 桌面 | Markdown 中心，塞 XML 别扭，**否** |
| Neovim / Emacs | Lua / Elisp | 能（extmark / overlay） | 能 | 能 | 能力够、受众错，**否** |
| JetBrains | Java/Kotlin 插件 | 能（Annotator / InlayHints） | 能 | 能 | 太重，除非团队已在 IDEA |
| Monaco / CodeMirror 本地 web app | 就是你自己写 | 完全可控 | 需自建后端 | 能 | 控制力最强，但等于造 app |
| **XML 生态**（Red Hat XML / Oxygen + RelaxNG/Schematron） | **写 schema 即可** | 校验 + 补全 | 不能 | 能 | **最便宜的"复用"**，但给不了 G2P preview |
| Excel / Sheets 插件 | Office.js / Apps Script | 弱（表格化） | 可调本地服务 | 能 | 只适合候选的**表格化**管理，不适合内联标记 |

**关于 XML 生态这条路值得单独说：** PFML 本质是 XML，所以"写一份 RelaxNG/Schematron + 用现成 XML 编辑器"能白拿校验和标签/属性补全，成本几乎为零。两个坑：(1) PFML 是**无根 fragment**，标准 schema 校验要先包一层合成根；(2) schema 给不了 G2P preview——而这恰好是价值最高的那部分。

---

## 6. 建议路线：分层，按需升级

不要一次定死形态，而是按"被证实的痛"逐层加码：

**L0 —— 先什么都不做（默认起点）**
优先用 `.txt` + TIFA 的 `-l`/`-L`；只有自动结果错了才写 PFML。同时**先量化**：真实数据集里到底有多少样本非 PFML 不可。

**L1 —— 一个薄 CLI（最高性价比，推荐第一个做）**
包一层 g2pflow，提供三个子命令：
- `check`：调 `parse_pfml()` 做全量校验（**直接消灭"静默丢样本"这个头号痛点**）
- `preview`：`convert_pfml()` 后把 word → reading → path → group → phoneme 打印出来，让"默认会切成什么/读成什么"可见
- `candidates`：把某词的候选列出来，供需求 3 使用

这一步顺便解决了"g2pflow 没有 CLI"这个所有方案共同的前置依赖。工作量：**天级**。

**L2 —— 加一份 schema**
RelaxNG/Schematron + 现成 XML 编辑器，白拿结构校验与补全。可选项，不阻塞。

**L3 —— VS Code 扩展（仅当 L1 被证明天天用时才做）**
扩展壳住 L1 的 CLI，把 `check` 结果变成 diagnostics，把 `preview` 结果变成 decoration/inlay hint。这才是"插件化复用现有软件"的正解，但**不应该作为起点**。

**明确不做：**
- ❌ 自研完备复文本编辑器
- ❌ PFML 的 vLabeler 插件（数据模型错配）
- ❌ 把"语法高亮/折叠"当成需求（那是第 4 痛点）

**另一条容易被忽视、但可能最省钱的路：根本不在 PFML 层修。**
- 系统性错误 → g2pflow 词典/转换器插件（一次解决一批）
- 单样本音频相关错误 → 下游 TextGrid（Praat / vLabeler）
- PFML 手工编辑 → 只留给"自动管线确实覆盖不到"的残余

---

## 7. 需要拍板的问题

1. **编辑者是谁？** 研究员 / 标注员 / 工程师？——直接决定宿主选型（VS Code 对非开发者是门槛）。
2. **频率真的低吗？** 需要在真实数据集上统计：多少样本必须用 PFML 而不能用 `.txt` 解决。
3. **修正的性质？** 系统性（→ 词典/插件）还是个案（→ 编辑器）？比例如何？
4. **是否需要回写？** 即"对齐后在 TextGrid 里改了，要不要反向生成 PFML"？如果要，这是独立的一块，且会显著改变工具形态。
5. **是否接受"先不做工具"？** 如果量化结果是"每千条改三条"，L0 + L1 可能就够了。

---

## 8. 建议先做的三个廉价实验（用数据决定，而不是靠讨论决定）

1. **统计实验**：拿真实数据集，只用 `.txt` 跑一遍 TIFA，看 `diagnosis.json` 里排在前面的可疑样本，有多少是分词/多音字问题、有多少能靠词典/`-l`/`-L` 解决。→ 直接回答"频率到底多低"。
2. **手写实验**：人工手写 10 条**真实**的 PFML 修正，记录每一步卡在哪。→ 这个结果直接决定工具形态（大概率卡在"不知道对不对"而不是"不好写"）。
3. **原型实验**：写个 30 行的 `parse_pfml` 包装，看"校验 + 预览"是否已经解决 80% 的痛。→ 如果是，L3 就没必要了。

---

## 9. 新旧工作流对比（TIFA 替代 HubertFA 之后）

### 9.0 先纠正一个事实

HubertFA、LyricFA、AudioSlicer、MinLabel **都不在 MakeDiffSinger 仓库里**，它们是 `openvpi/dataset-tools` 下的 GUI 应用：

| 应用 | dataset-tools 里的定位（README 原文） |
|---|---|
| AudioSlicer | "RMS-based automatic audio slicing with Audacity CSV marker support" |
| LyricFA | "Lyric forced alignment using FunASR Paraformer (Chinese)" |
| HubertFA | "HuBERT phoneme forced alignment with Praat TextGrid output" |
| MinLabel | "Audio labeling tool with G2P conversion (Mandarin/Cantonese/Japanese)" |

MakeDiffSinger 的 `acoustic_forced_alignment/` 里只有 **MFA** 那一套脚本（`reformat_wavs.py` / `enhance_tg.py` / `combine_tg.py` / `slice_tg.py` / `build_dataset.py`），**没有 HubertFA**。所以其实存在两条"旧流程"：官方的 MFA 流程，和你描述的 GUI 工具链流程。下面按 GUI 工具链讲。

### 9.1 旧流程（AudioSlicer → LyricFA → MinLabel → HubertFA）

| 步 | 工具 | 输入 | 输出 | 实际职责 |
|---|---|---|---|---|
| 1 | **AudioSlicer** | 原始长音频 | `<base>_NN.wav` 切片 + `<base>.csv`（Audacity marker） | **仅静音切分**（RMS 阈值 −40dB，min_length 5s，min_interval 300ms）。不产出文本、不产出音素 |
| 2 | **LyricFA** | 已切片的 `[lyricName]_xxx.wav` + 原始歌词 `[lyricName].txt` | `<name>.lab`（音节）+ `<name>.json`（matched_text/matched_phonetic，给 MinLabel 预载） | FunASR Paraformer **ASR** + 歌词序列匹配。**没有时间戳、没有 TextGrid**。不做边界检测 |
| 3 | **MinLabel** | 切片 wav + 上一步 json | `<name>.lab`（空格分隔音节）+ `<name>.json`（`lab`/`lab_without_tone`/`isCheck`） | **带音频播放的逐条人工校对** + G2P（pinyin / romaji / jyutping）。**完全不碰 TextGrid** |
| 4 | **HubertFA** | wav + `.lab` | `<baseName>.TextGrid`，**2 层**：`words`、`phones` | `.lab` → 音素边界。ONNX，SOFA/FoxBreatheLabeler 系 |
| 5 | build_dataset.py | wav + TextGrid | `transcriptions.csv`（`name, ph_seq, ph_dur`） | 出 DiffSinger 数据集 |

注意第 3 步 MinLabel 是**唯一的"人眼过一遍"环节**，而且是**逐条、带音频**的。

### 9.2 新流程（TIFA 替代 HubertFA）

TIFA 的定位：**音频 + 转录 → TextGrid**。转录按 `.pfml` > `.txt` > `.lab` 优先查找；**只有 `.pfml` 被当标记解析**；内部用 g2pflow 自己完成 G2P（默认管线含中/粤/日/英，自带 4 本词典）。

| 步 | 工具 | 输入 | 输出 | 变化 |
|---|---|---|---|---|
| 1 | **AudioSlicer** | 原始长音频 | 切片 + csv | **不变，仍然必需**（TIFA 不做切分） |
| 2 | 逐条转录 | — | `.txt`（或需要覆盖时 `.pfml`） | **LyricFA / MinLabel 的 G2P 职能被吸收** |
| 3 | **TIFA** `infer.py` | 切片音频 + 转录 | TextGrid，**3 层**：`texts` / `words` / `phones` | **替代 HubertFA**；多了 `texts` 层；自带 G2P |
| 4 | 检查环 | `statistics/diagnosis.json`、`scores.json` | 干预决策 | **新增**：无参考自洽性指标，可疑样本排前面 |
| 5 | → DiffSinger csv | TIFA 输出 | ? | **缺口，见 9.4** |

### 9.3 三个职能的去向（这是关键）

1. **HubertFA → TIFA**：**一一对应**。都是"转录 → 音素边界 TextGrid"。但 TIFA 多产出一层 `texts`（语义词文本），而 HubertFA 只有 `words`+`phones`。下游吃 TextGrid 的脚本要注意层数变化。
2. **MinLabel 的 G2P 职能 → 被 TIFA 吸收，变成冗余**。TIFA 自己调 g2pflow 做 G2P，不再需要预先存在 `.lab` 里的读音。
3. **MinLabel 的校对职能 → 没有被任何东西替代**。TIFA 没有编辑器，只有 `diagnosis.json` 和 `--plot` 相似度图——那是**检查/筛选**，不是**编辑**。这个空缺正是"写 PFML 覆盖"要填的位置。
4. **MinLabel 的逐条音频审阅职能 → 部分被替代**：过去是"每条都过一遍"，现在是"只看被 diagnosis 标记为可疑的"。这是从**全量人工**到**抽样人工**的转变。
5. **LyricFA**：核心价值是"从音频得到转录 + 把整首歌词匹配到切片"。**如果你本来就有干净歌词文本，这一步可以整个跳过**；如果只有音频没有歌词，它作为 ASR 仍有价值（TIFA 需要转录，虽然它对标注错误鲁棒）。

### 9.4 新流程的两个缺口（必须补，否则流程走不通）

1. **TIFA 输出 → DiffSinger `transcriptions.csv` 没有官方转换器**。TIFA 自己的训练格式是 `index.csv`（`name, language, phones, durations`），而 DiffSinger 要 `name, ph_seq, ph_dur`。两个仓库都**没有任何一方提到对方**（全 org 搜 `TIFA` 只命中 TIFA 仓库自己）。
2. **TextGrid 层数/符号集不一致**：TIFA 是 3 层 + 自带全局符号 `AP/SP/EP/GS/sil/br/pau`；旧流程的 `enhance_tg.py` 是往 2 层 TextGrid 里补 SP/AP。`build_dataset.py` 能否直接吃 TIFA 的 TextGrid 需要实测。

另外，**没有任何官方文档声明"TIFA 取代 HubertFA/LyricFA/MFA"，也没有新旧流程迁移图**。MakeDiffSinger README 最后更新是 2023-10-17，早于 TIFA。所以下面的"新流程"有一部分是基于工具输入输出推断的，我会标注。

### 9.5 两种迁移策略

**策略 A（最小改动，零新工具）**：保留 AudioSlicer → LyricFA → MinLabel 全链，**只把 HubertFA 换成 TIFA**。因为 TIFA 接受 `.lab`，所以直接换最后一步即可。
- 优点：立刻能跑，不动任何东西。
- 缺点：白付 LyricFA + MinLabel 的成本，而且完全用不上 TIFA 的"直接喂文本"优势；PFML 编辑需求也不会出现。

**策略 B（目标形态，省掉两个工具）**：
```
原始音频
  └─ AudioSlicer（静音切分，5–15s）
       └─ 每段一份 .txt（需要覆盖时改 .pfml）
            └─ TIFA infer.py  →  TextGrid(3层) + scores.json + diagnosis.json
                 └─ 诊断驱动的修复环：只看 diagnosis.json 前排的可疑样本
                      └─ [缺口] 转换 → DiffSinger transcriptions.csv
```
- 优点：LyricFA + MinLabel 整个砍掉；G2P 交给 TIFA；人工从"全量逐条"降到"只修可疑样本"。
- 代价：需要补 9.4 的转换器；需要确认切片粒度对 TIFA 是否合适（TIFA 无切分代码，训练用 5–15s 片段，但**没有官方说明**推理时能否/是否该整首喂入——这条是推断）。

### 9.6 对"要不要做 PFML 编辑器"的影响

新旧对比让第 0 节的结论更硬：

- 编辑面从**"每条都过的 GUI 校对台"**（MinLabel）变成了**"只处理被诊断标记的少数样本"**。工作量下降一个数量级。
- 所以新流程里真正缺的**不是编辑器**，而是**一个诊断驱动的修复台**：读 `diagnosis.json` → 定位可疑样本 → 展示"为什么可疑"（分词？多音字？）→ 写出对应的 `.pfml` 覆盖 → 重跑该样本。
- 这依然不需要复文本编辑器。它需要的是**流程编排 + 校验 + 预览**，也就是第 6 节的 L1（薄 CLI），只是入口从"手工打开文件"变成"从 diagnosis.json 进"。
- **副产品结论**：`diagnosis.json` 里"可疑样本"的分布，正好就是第 8 节"统计实验"要的数据。跑一次 TIFA 就能同时拿到"需求频率"和"可疑类型"两个答案。

### 9.7 LyricFA 的输出格式（源码确认）

LyricFA 分两个独立动作，各产出一样东西：

**动作 1「Run ASR」→ `.lab`**（`AsrThread::run`）

- 每个切片 wav 一个 `.lab`，同名同目录。
- 内容是**一行纯文本的 ASR 识别结果**，README 描述为"空格分隔的汉字或拼音"。
- 勾选 "ASR result saved as pinyin" 时，汉字经 `hanziToPinyin(..., ManTone::NORMAL, ...)` 转成**无调拼音**后再写。
- **没有时间戳、没有音素、没有 TextGrid。** 就是一串词/音节。
- 附带限制：ASR 内部还会再切片（`AudioUtil::Slicer(160, 0.02f, 160, 160*4, 500, 30, 50)`），**连续发音段超过 60 秒会直接报错**，要求手工切分后重跑。

**动作 2「Match Lyrics」→ `.json`**（`LyricMatcher::save_to_json`）

精确 schema（源码原文，注意后两个字段赋的是**同一个值**）：

```cpp
QJsonObject obj;
obj["raw_text"] = text;              // matched_text：匹配到的歌词文本
obj["lab"] = phonetic;               // matched_phonetic：对应拼音
obj["lab_without_tone"] = phonetic;  // ← 与 lab 相同，并非真的去调
```

即：

```json
{
  "raw_text": "<匹配到的歌词文本>",
  "lab": "<对应拼音>",
  "lab_without_tone": "<与 lab 相同>"
}
```

- `matched_text` / `matched_phonetic` 来自 `SequenceAligner::find_best_match_and_return_lyrics`，即**从整首歌词里切出的、与这段音频 ASR 最匹配的那一片段**及其拼音。
- 匹配失败时仍会写文件，三个字段全是空字符串（`save_to_json(jsonPath, "", "")`）。
- **注意 `.lab` 和 `.json` 的 `lab` 字段不是一回事**：`.lab` 是**原始 ASR 文本**，`.json` 的 `lab` 是**歌词匹配后的拼音**（经过歌词校正）。

**MinLabel 接手后同一份 json 的变化**（`MainWindow::saveFile`）：

```cpp
writeData["lab"] = labContent;              // 空格归一化
writeData["lab_without_tone"] = withoutTone; // 这次是真的去调版本
writeData["isCheck"] = true;                 // ← MinLabel 新增的"已校对"标记
```

- MinLabel 读取时只看 `lab`（`readData["lab"]`）做预载；文件列表的勾选状态读 `isCheck`（`jsonObj.value("isCheck").toBool(false)`），所以 LyricFA 产出的 json 一律显示为**未校对**。
- MinLabel 保存时是**新建对象**，因此 `raw_text` 会被丢掉。
- 另有「Convert lab to project file」把已有 `.lab` 反推成 json（不含 `isCheck`）。

**一句话总结：LyricFA 的输出是"每段的歌词文本 + 拼音"，纯预载提示，不含任何时间或音素信息。它的唯一消费者是 MinLabel。**

**对流程讨论的含义**：如果按策略 B 砍掉 MinLabel，LyricFA 的 `.json` 就**没有消费者了**——它存在的唯一目的就是省掉人在 MinLabel 里打字。届时 LyricFA 只剩 ASR 的 `.lab` 还有价值（作为 TIFA 的转录来源）。这反过来印证：策略 B 下 LyricFA 至多降级为"纯 ASR 工具"，而若你本来就有干净歌词文本，它可整体移除。

---

## 10. 需求定案（讨论中）：新中间工具的定位

### 10.1 先把"断链"拆精确

说"断链"是对的，但一条链断在哪几节，决定了工具要做什么。逐节看：

| 环节 | 旧责任人 | 新情况 | 状态 |
|---|---|---|---|
| A. 逐段转录的产生 | LyricFA（ASR+歌词匹配）→ MinLabel（人工核对） | LyricFA 仍在，`.lab` 仍可喂给 TIFA；也可直接手写 `.txt` | **降级，未断** |
| B. G2P 读音的确定 | MinLabel（拼音/罗马字/粤拼） | TIFA/g2pflow 内部自动完成，且候选自动打分选择 | **被吸收，人工干预口消失** |
| C. 人工纠正 G2P / 分词的**载体** | MinLabel 写 `.lab` | 只剩 PFML，无工具 | **真断** |
| D. 逐段音频人工复核 | MinLabel（带播放器，逐条过） | 变成 `diagnosis.json` 分诊 + TextGrid 复核 | **迁移，未断** |
| E. TIFA 输出 → DiffSinger `transcriptions.csv` | build_dataset.py | 无转换器 | **真断（与编辑无关）** |

所以真正断的是 **C**（纠正的载体）和 **E**（数据集转换）。B 是被吸收，D 是迁移。

**这个区分很重要**：C 断的是"人工决定的落点"，不是"文本编辑"。B 被吸收恰恰说明**人不再需要"输入读音"**——人只需要在自动结果错的时候**做决定**。

### 10.2 多语言问题要收窄：真正的触发条件是"同字形歧义"

你说"多语言需要引入 PFML 指导 TIFA"。这个判断成立，但要收窄，因为 **TIFA 的 `-l zh -L en` 已经能处理一部分混语**：g2pflow 在无显式 scope 时按管线自身逻辑路由，而**字形本身可区分语言**（汉字→zh、假名→ja、拉丁→en）。

PFML 真正不可替代的场景是**字形无法区分语言**：

- **汉字 / 日语汉字**：`東京` 是中文还是日文？都是汉字，字形给不出答案。← 这正是你第一条消息里说的"日语汉字对应发音的复杂问题"
- **普通话 / 粤语**：`唔该` 都是汉字。
- 同字形跨语言的其他组合。

反过来，**纯假名的日文片段、纯拉丁的英文片段，不需要 PFML**——字形已经说明了语言。

这个收窄带来一个关键推论：**工具不需要让人"标注多语言"，只需要让人"裁决字形无法区分的那些片段"。** 而后者的数量远小于前者，并且**可以被自动检测出来**（找出有效语言未定的汉字片段）。

### 10.3 工具重新定义：分诊 + 歧义点决策台（不是编辑器）

把 10.1 和 10.2 合起来，工具的形态就清楚了。它要做的三件事：

1. **检测（Detect）**——自动找出"管线不得不猜"的点：
   - 语言歧义：字形无法确定语言的片段（10.2）
   - 多音字歧义：`scores.json` 里 `alternatives` 多于一个的词
   - 分词歧义：G2P 的切分与实际不符
2. **呈现（Present）**——把歧义点连同决策所需的上下文摆出来：音频、TIFA 解析出的 `texts`/`words`/`phones` 三层、候选列表及分数、`diagnosis.json` 的可疑度。
3. **落盘（Emit）**——记录人的决定，产出 PFML，重跑该样本。

**关键：人的工作从"输入/打字"变成了"在少数歧义点上做决定"。** 决定是离散的（从 2–3 个候选里挑一个 / 给一段标语言 / 挪一个边界），不是连续的文字编辑。所以这不是复文本编辑器，是个**决策台**。

### 10.4 关键架构决定：操作对象模型，不自己实现 PFML

这是整个方案里最重要的一条技术决定：

**不要自己解析/生成 PFML 文本，直接操作 g2pflow 自己的对象模型（`G2PWord` / `G2PReading` / `G2PPath` / `G2PGroup`），序列化交给 `to_pfml()`。**

理由：
- g2pflow 已经提供了**无损往返保证**：`G2PPipeline().convert_pfml(to_pfml(words)) == words`。
- 自己实现一套 PFML 解析/生成，等于重写一份规范并承担它所有的边界情况（无根 fragment、转义、标签填充规则、语言继承、同级约束……）。完全没必要。
- 有了这条，**"PFML 不好编辑"这个问题本身就被消解了**——工具内部根本不存在"编辑 PFML 文本"这个动作，只有"改对象模型 → 序列化"。人看到的是结构化的决定，PFML 只是产物。

推论：工具的输入是对象模型，输出也是对象模型，PFML 是两者之间的持久化格式。**这也顺带回答了"要不要做回写"**：如果能把 TextGrid 反推回对象模型（从 `texts`/`words`/`phones` 三层重建 direct word），就能实现"修完 TIFA 的结果再生成 PFML"的闭环。

### 10.5 三个检测信号里，TIFA 已经白送了两个

| 信号 | 来源 | 是否需要新做 |
|---|---|---|
| 多音字歧义 | `scores.json`（`alternatives` + `chosen`） | **已有** |
| 样本级可疑度 | `diagnosis.json`（agreement/confidence/determinacy/monotonicity） | **已有** |
| 语言歧义 | 无 | **需新做**（g2pflow 侧分析：找出有效语言未定的同字形片段） |
| 分词歧义 | 间接（`texts` 层 + diagnosis） | 需设计规则 |

**这大幅缩小了工具的工作量**：TIFA 已经把"哪里可能错"报出来了，工具主要是在这些信号上加一个决策 UI，外加一个语言歧义分析器。

### 10.6 形态选择与仍未验证的前提

**形态**：由于决策需要"音频 + 三层对齐 + 候选分数"同时在场，前面推荐的"VS Code 装饰层"不再是首选——把波形和对齐摆在文本装饰里很别扭。更贴合的是**一个小型本地 web app**（后端薄封装 g2pflow + TIFA，前端 CodeMirror/WaveSurfer 只做呈现），或者**一个带交互提示的 CLI**。这是对第 6 节建议的**修正**，修正的原因是工具形态从"偶尔改文本"变成了"带音频的决策台"。

**但"必有"仍然依赖一个尚未验证的事实**：

> **目标数据集是单语还是混语？**

- **单语**（例如只有中文歌）：语言歧义几乎不存在，多音字由 TIFA 候选打分兜住，人工干预仍是低频 → 仍然是**鸡肋**，L1（薄 CLI + 校验 + 预览）就够，不必做 GUI。
- **混语**（尤其 zh/ja 或 zh/yue 共用汉字）：每个含汉字的片段都可能是歧义点 → 干预变成**常规动作** → 这时才真的**必有**，GUI 决策台才被证明合理。

所以结论应该是：**链必须闭合，但"必有"取决于数据集的语言构成。** 这是唯一还需要事实来定的前提，而且它可以被低成本验证（见 10.7）。

### 10.7 下一步（把讨论收敛成一个可验证动作）

拿一小批**真实的、混语的**数据，只用 `.txt` 跑一遍 TIFA（不加任何 PFML），然后统计：

1. `diagnosis.json` 前排可疑样本有多少、都是什么类型；
2. `scores.json` 里有多候选的词占比多少；
3. **纯汉字片段（有效语言未定）有多少** ← 这个数直接决定语言歧义是否常规；
4. 其中多少能靠加词典/插件系统性解决、多少必须逐个写 PFML 覆盖。

**第 3 项就是"鸡肋 vs 必有"的判据。** 这个实验不需要写任何工具代码，只需要跑一遍 TIFA 加一段统计脚本。

---

## 附：参考

- TIFA — https://github.com/openvpi/TIFA
- g2pflow — https://github.com/openvpi/g2pflow （PFML 1.0 规范见 `docs/pfml.md`）
- dataset-tools / MinLabel — https://github.com/openvpi/dataset-tools
- vLabeler — https://github.com/sdercolin/vlabeler
- MakeDiffSinger（`.lab` 的 de-facto 规范与 `validate_labels.py`）— https://github.com/openvpi/MakeDiffSinger
