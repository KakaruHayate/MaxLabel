# MaxLabel —— 实施计划 v0.2

日期：2026-10-01
状态：待评审（下一轮仍不实现，先收敛决策）
关联：[pfml-editing-requirements.md](./pfml-editing-requirements.md)

---

## 0. 定位

**MaxLabel = PFML 的编辑与校对工具**，位于 TIFA 之前。

```
AudioSlicer ──► (文本 / .lab+json / .pfml)
                        │
                        ├──► MaxLabel ──► song.pfml          ← 我们在这里
                        │
                        └──► TIFA Label / tifa_ggml_cli ──► TextGrid + diagnosis.json
                                                             │
                                                             └──► 后续工具（不在本链条内）
```

- **输入**：`wav` +（可选）`.pfml` / `.txt` / `.lab` / `.json`；只有 wav 时从零搭建
- **输出**：**只有 `.pfml` 一种**
- **不做**：TIFA 输出侧的转换、切片修正（你已明确排除）

---

## 1. 重大架构修正：不需要 Python sidecar

上一版计划的核心假设（"g2pflow 没有 CLI，所以必须有 Python sidecar"）**已经被 tifa.cpp 推翻**。

`KakaruHayate/tifa.cpp`（MPL-2.0，C++17，CMake）已经提供了 C++ 版的完整 G2P 与 PFML 支持，且以**静态库** `tifa_ggml` 形式产出，可 `add_subdirectory` / `FetchContent` 链接。

### 可直接复用的 API（`src/g2p/`）

| 能力 | API | 用途 |
|---|---|---|
| G2P 管线 | `g2p::Pipeline::from_config(g2p_json, dict_dir)` | 从 GGUF 内嵌配置构建 |
| 文本 → 词 | `Pipeline::convert(text, languages) → vector<Word>` | 实时 G2P 预览 |
| PFML 识别 | `looks_like_pfml(text)` | 分流 |
| PFML → 词 | `convert_pfml(pipeline, text, languages) → vector<Word>` | **导入 PFML** |
| 候选网格 | `encode_paths(...) → CandidateGrid` | **发音候选弹窗的数据源** |
| 音素校验 | `resolve_phoneme(phone, langs, lookup, global_symbols, stop_symbols) → {id, symbol}` | **音素合法性检查（id == -1 即非法）** |
| 单字读音 | `PinyinEngine::readings(ch)` / `query_raw(chars)` | 多音字候选 |
| 数据模型 | `Word{text, language, readings}` / `Reading{paths}` / `Path = vector<Group>` / `Group{script, phonemes}` | **与 g2pflow 同构** |
| 标注读取 | `read_phones_file` / `read_textgrid_tier` / `read_transcriptions_csv` | 导入 `.lab` / TextGrid / csv |

`CandidateGrid` 里带 `lexicon[W]`，每个词有 `Candidate{reading, phonemes[], scripts[]}` —— **这正是需求 3"指定发音"需要的候选列表，现成的**。

`resolve_phoneme` 返回 `id = -1` 就是"不在音素表里" —— **这正是你要的"打波浪线 + 提示"的判据，也是现成的**。

### 两个必须先补的缺口

**缺口 A：PFML 只有解析，没有序列化。**
`src/g2p/pfml.h` 只导出了 `looks_like_pfml` 和 `convert_pfml`（文本 → `vector<Word>`），**没有 `to_pfml`（`vector<Word>` → 文本）**。而 MaxLabel 的核心产出就是 PFML。

> **建议：把这个序列化器实现到 tifa.cpp 里**，与解析器同处一个模块。这样能就近保证往返一致性（`convert_pfml(to_pfml(words)) == words`，即 g2pflow 那条无损保证），也能让 TIFA Label 未来复用。

**缺口 B：G2P / PFML 目前是 internal API。**
`src/g2p/*.h` 明确写着 "Internal API (not exported from the library)"，命名空间是 `tifa_ggml::internal::g2p`；而 CMake 里 `src/` 是 **PRIVATE** include，公开头只有 `include/tifa_ggml/`（里面是 `Model` / `AlignRequest` / `AlignResult` 等对齐相关类型，**没有 G2P**）。

三个选择，见 §3-D1。

---

## 2. 本轮已定案

### D1′ 技术栈：Qt6 / C++，链接 `tifa_ggml`

- 沿用你的决定：**Qt6/C++**（不用 Electron，不用裸 Win32）。Qt6 SDK 需要下载安装（你已确认）。
- **不做 Python sidecar**：G2P 与 PFML 全部走 `tifa_ggml`。
- 复用 tifa.cpp 的 `dr_libs`（wav/flac/mp3 解码）与 `audio_io`。
- 许可证：MaxLabel 与 tifa.cpp 同为 **MPL-2.0**，兼容；Qt6 动态链接满足 LGPLv3。

### D2′ 不要重工程文件：同名同目录 + 优先级

```
song.wav
song.pfml              ← 内容真源，唯一产出
song.txt               ← 纯文本转录（可选输入）
song.lab               ← 旧 MinLabel 音节/音素（可选输入）
song.json              ← 旧 MinLabel 工程（可选输入）
song.maxlabel.yml      ← 可选，只存 UI 状态；删掉不丢任何内容
```

- **读取优先级沿用 TIFA**：`.pfml` > `.txt` > `.lab`；`.json` 作为附加信息合并进来。
- **产出只有 `.pfml`。**
- 只有 wav 时 → 从零搭建，人工录入文本，产出 `.pfml`。
- `song.maxlabel.yml` 是**可选**的轻量 sidecar，只放校对勾选 / 播放位置 / 备注这类 UI 状态。**工具必须在没有它的情况下也能正常工作**，这样才不违背"同名同目录"约定。

### D3′ g2p.yaml 与音素表：不用配置，它们在 GGUF 里

你说"先默认整个社区用同一份、中心化"。实际上比这更彻底：

> `Pipeline::from_config()` 的注释写着：**"Build from the JSON embedded in the GGUF (`tifa.vocab.json` → `"g2p"`), which follows `configs/g2p.yaml`"**；README 也说 **"GGUF 内嵌词表与 G2P 管线配置"**。

**也就是说 G2P 配置、词表、`global_symbols`、`stop_symbols` 全都内嵌在模型文件里，没有外部配置需要管理。** 这彻底消除了"版本不一致导致预览骗人"的风险。

### D4′ 音素校验：用 `resolve_phoneme` 做实时检查

- 插入音素时对每个符号调 `resolve_phoneme`；`id == -1` → **波浪线 + 提示**（你要的行为）。
- 插入面板给两类来源：**全局符号**（`AP` / `SP` / `EP` / `GS` / `sil` / `br` / `pau`）+ **模型词表里的全部合法音素**（带补全）。
- 关于**垫音 n/m**：BreathLab 检测的是 AP/SP/V 时间线，**垫音是浊音（V），不会被标成 AP**，所以 BreathLab 帮不上忙 —— 这类插入仍需人工，正好由本功能覆盖。

### D5′ 只管上游，不管下游

- 不管 TIFA 输出 → DiffSinger csv 的转换（会有后续工具接档）。
- 不管切片错误（需要重新切，不是拖一下能解决的）。

---

## 3. 还需要决定的事（下一轮的重点）

按重要性排序。前三条阻塞开工。

### ⬛ D-A（阻塞）tifa_ggml 的 G2P/PFML API 怎么用

| 方案 | 优点 | 缺点 |
|---|---|---|
| **(a) 上游提升为公开 API**（推荐） | 干净、类型安全、双方都能用；同作者、同许可，改动小 | 需要先动 tifa.cpp |
| (b) MaxLabel vendored 拷贝 `src/g2p/` | 不动上游 | 代码分叉，PFML 规范演进要手动同步，**两份解析器容易不一致** |
| (c) 硬 include `src/` 目录 | 最省事 | 依赖内部布局，上游一重构就断 |

**建议 (a)**，并把缺口 A 的 `to_pfml` 一并加上。

### ⬛ D-B（阻塞）PFML 序列化器写在哪

建议放 tifa.cpp 的 `src/g2p/pfml.cpp`，与解析器同处，附往返测试。若你希望 MaxLabel 自持，也可以，但要接受"两个仓库各有一份 PFML 规范实现"。

### ⬛ D-C（阻塞）G2P 配置与词表的获取方式 → 决定发布体积

现在配置内嵌在 **TIFA 主模型 GGUF** 里。MaxLabel 只需要 G2P 部分，不需要对齐模型（几百 MB）。所以：

- 方案 1：MaxLabel 直接加载完整 TIFA GGUF（简单，但包大 / 需用户指路）
- 方案 2：**请 tifa.cpp 提供"只导出/加载 vocab + g2p 配置"的轻量入口**（推荐；MaxLabel 可以带一个几百 KB 的小文件）

这直接决定 MaxLabel 发布包里带不带模型、带多大。

### D-D 音频解码与播放选型

- tifa.cpp 用 `dr_libs`（wav / flac / mp3）。但 TIFA 官方接受 **wav/flac/opus/mp3/aac/ogg**。
- 播放后端需要另选（Qt Multimedia / miniaudio / 各平台原生）。
- 要决定：**是否支持 opus/aac/ogg**（要额外解码器），以及播放走 Qt Multimedia 还是自带。

### D-E "音素行"的渲染方案

文本要按语言上底色、按词划边界，**还要把音素显示在对应词的下方**。候选做法：
- 双行同步视图（文本行 + 只读音素行，同一套坐标）
- Scintilla annotation（按行，不是按词，粒度对不上）
- 自绘控件（最灵活，成本最高）

歌词短、条目少，**双行同步视图**大概最划算，但需要定。

### D-F 是否集成"就地跑一次 TIFA 对齐预览"

`tifa_ggml` 就在手边。编辑完 PFML 后**立刻跑一遍对齐看结果**，价值极高（相当于所见即所得的闭环）。代价是要加载对齐模型 + 推理。
→ 决定是否进 v1，还是留到 v2。

### D-G 与 TIFA Label 的分工确认

tifa.cpp 已自带 **TIFA Label（Electron）**，功能是"导入音频 → 第一遍对齐 → BreathLab → 2PASS → 输出 TextGrid + diagnosis.json"，**且已支持 PFML 作为输入**。

所以 MaxLabel 的定位是**它的上游**（生产/修正 PFML）。需要确认这个分工，避免重复造轮子——尤其是：TIFA Label 里那些"标注来源选择""批量进度"之类的 UI，MaxLabel 要不要保持一致。

### D-H 其余

- **sidecar 的确切命名与字段**（`song.maxlabel.yml`？还是别的）
- **`.json`（MinLabel 工程）如何参与合并**：只有 `lab` / `lab_without_tone` / `isCheck` 三个字段，`isCheck` 可映射为校对状态
- **UI 语言**：中文界面为主？是否一开始就做 zh/en
- **CI/发布**：MaxLabel 的三端自动 release 是否要连带构建 `tifa_ggml`（会 FetchContent 拉 ggml，构建变重、时间变长）
- **快捷键默认表**（见 §5）

---

## 4. 顺带澄清两个你没理解的点

### 4.1 什么是"混语歧义"

语言自动识别有两条路：
- **按字形**：汉字→zh、假名→ja、拉丁→en、谚文→ko。**这条路对"中文里夹英文"完全够用**——你举的例子属于这一类，不难。
- **按模型**：`fast-langdetect` 这类小模型猜语言。

**歧义只发生在"字形区分不了"的时候**，也就是**两边都是汉字**：

- 「東京」是中文（dōng jīng）还是日文（tōkyō）？
- 「唔该」是普通话还是粤语？

GPT-SoVITS 的 split-lang README **自己承认**"纯汉字日文与中文混合的情况仍会误判"，并在代码里对非英文片段直接强制使用用户选的语言（注释：`# 因无法区别中日韩文汉字,以用户输入为准`）。

**所以这个功能的正确形态是：自动切分给建议 + 把"有效语言未定的汉字片段"高亮出来 + 人工一键指定。** 人工指定那一步才是可靠路径——自动那步只是省事。这也解释了为什么这个 UI 在开源生态里是空白：**没人做过，因为没人解决得了自动那半。**

### 4.2 10.7 节还在（在需求文档里）

`pfml-editing-requirements.md` 的 §10.7 是"下一步：把讨论收敛成一个可验证动作"，内容没丢。核心是那个统计实验：真实混语数据只用 `.txt` 跑一遍 TIFA，统计 diagnosis 前排可疑样本数、多候选词占比、**纯汉字歧义片段数量**，以及其中多少能靠词典/插件系统性解决。

既然你已经决定"功能是要做的"，这个实验的定位就从"决定要不要做"降级为"决定优先级和工作量估算"。它仍然值得跑，但不再是阻塞项。

---

## 5. 架构与 UI（在 §2 定案基础上）

```
┌──────────────────── MaxLabel（Qt6 / C++17）────────────────────┐
│  段落列表  │  可标注富文本区                    │  音频面板     │
│  虚拟列表  │  · 语言底色  · 分词边界  · 音素行   │  波形/频谱    │
│  ☑/☐ 校对  │  · 点击词 → 候选弹窗  · 插入音素    │  选区播放     │
└──────────────────────────────┬─────────────────────────────────┘
                               │ 直接链接（同进程）
┌──────────────────────────────┴─────────────────────────────────┐
│  tifa_ggml（静态库，MPL-2.0）                                    │
│  g2p::Pipeline / convert_pfml / encode_paths / resolve_phoneme  │
│  + [缺口 A] to_pfml()      + [缺口 B] 提升为公开 API             │
│  vocab / g2p 配置 / global_symbols ← 全部来自 GGUF               │
└─────────────────────────────────────────────────────────────────┘
```

**内部一律操作 `Word`/`Reading`/`Path`/`Group` 对象模型，不碰 PFML 文本**；导入导出都过 `convert_pfml` / `to_pfml`。这样"PFML 不好编辑"在工具内部根本不存在。

### 数据模型

```
Segment {                      // 一条 = 一段音频 + 一行文本
  id, text,                    // text 是内容真源
  audio: { path, start, end },
  spans:  [ LangSpan ],        // 语言区间（source: auto|manual）
  words:  [ WordBoundary ],    // 分词切点
  overrides: [ Override ],     // 锚定
  review: { checked, note }
}
Override {
  target: { kind: word|span|pos, anchor_id, text_range },
  kind:   pin_reading | pin_phonemes | insert_phoneme,
  value, stale
}
```

`Override` 统一承载需求 3 与 4。锚定照抄 **OpenUtau** 的做法（override 列表与自动结果分开存，每次重跑 G2P 后重新应用）；文本变动时置 `stale` 并提示，**不静默丢弃**。

### 快捷键（Aegisub 式，分上下文）

**全局**：`Z`/`X` 上一条/下一条 · `Enter` 提交 · `Shift+Enter` 提交并下一条 · `Ctrl+Z`/`Ctrl+Y` 撤销/重做 · `Ctrl+S` 保存

**音频**：`Space`/`B` 播放停止切换 · `R` 播放当前段 · `S` 播放选区 · `Q`/`W` 前/后 500ms · `E`/`D` 选区首/尾 500ms · `T` 播放到文件尾 · `H` 停止 · `A`/`F` 视图左/右移

**编辑**：`1`/`2`/`3`/`4` 把选中区间设为对应语言 · `/` 加/删分词边界 · `双击词` 弹候选 · `Ins` 插入音素 · `Ctrl+Shift+P` 非词汇音面板 · `Tab` 跳到下一个歧义点

**数据安全**：任何修改**立即落盘 + 原子写**，不留 pending 状态。（Aegisub 的教训：音频标记拖动是 pending 的，切行会直接丢弃，除非开 auto-commit。我们从模型上避免这个坑。）撤销用整模型快照 + 打字合并成组。

---

## 6. 里程碑

| 阶段 | 内容 | 依赖 |
|---|---|---|
| **M-1** | 上游改动：tifa.cpp 提升 G2P/PFML 为公开 API + 实现 `to_pfml` + 往返测试 | D-A / D-B |
| **M0** | 数据链路：导入（pfml/txt/lab+json）→ 对象模型 → 导出 pfml；校验闸门；无 UI | M-1, D-C |
| **M1** | 文本编辑器 + 语言切分（底色 / 歧义高亮 / 手动指定 / 分词编辑 / 自动保存撤销） | M0, D-E |
| **M2** | 音频面板（波形/频谱/选区/播放 + text-first 配对 + Aegisub 式快捷键） | M0, D-D |
| **M3** | 发音决策（候选弹窗 + 锚定 + 插入音素 + 非词汇音面板 + 实时校验） | M1, M-1 |
| **M4** | 分诊与批量（diagnosis 列表 / 批量应用 / 导出词典条目） | M3 |

---

## 7. 还没考虑周到的点

1. **`.pfml` 与 `wav` 不同名怎么办**——约定是同名同目录，但实际数据里常有 `xxx_001.wav` 这种带序号的切片。配对规则要定死（是取最后一个下划线前的部分，还是允许显式映射）。
2. **锚定在文本变动后是否失效**——必须定义并给提示。
3. **音素行的粒度**：一个词对应多个 group、每个 group 多个音素。显示到什么层级要定（全展开会很长）。
4. **readings 与 paths 是两层**——弹窗默认只让人选 reading，path 作为高级项，否则界面会爆炸。
5. **批量与"回灌源头"**——系统性错误应写成 g2pflow 词典条目而不是逐个改标记。工具应能一键导出词典条目，这是让工具"能自我消减"的关键。
6. **垫音是"新增"不是"纠错"**——`diagnosis.json` 发现不了"少了个垫音"，所以这类编辑是纯人工创作动作，UI 上要单列入口。
7. **`isCheck` 的映射**——旧 `.json` 里的 `isCheck` 可以映射成校对状态，但要注意 MinLabel 保存时会丢掉 `raw_text`。
8. **撤销粒度**——打字用 commit_id 合并成一组，结构操作各自成组。
9. **大工程性能**——段落列表虚拟化，音频按需解码。
10. **i18n**——参考项目都有完整 i18n，建议一开始留出 zh/en。
11. **色盲友好**——语言底色是核心视觉，不能只靠红绿，要配图例。
12. **决策日志**——记录"为什么这里有覆盖"，便于复现和交接，成本低回报高。
13. **CI 构建时间**——`tifa_ggml` 会 FetchContent 拉 ggml，三端 × 多后端矩阵会让 release 变慢；要考虑是否只构建 CPU 后端，或缓存 ggml。
14. **音频格式与解码器**（见 D-D）——若支持 opus/aac/ogg 需额外依赖。
15. **"从零搭建"是否需要 ASR 辅助**——只有 wav 时是否接一个 ASR 给出初稿（LyricFA 那条路），还是纯手打。

---

## 8. 风险

| 风险 | 缓解 |
|---|---|
| 依赖 tifa.cpp 内部 API，上游重构会断 | 走 D-A 方案 (a)，把接口正式化 |
| PFML 规范演进（g2pflow 1.0 之后） | 序列化器与解析器同处，规范变更只改一处 |
| 发布体积（模型/词表） | 走 D-C 方案 2，只带轻量 vocab+g2p |
| 范围膨胀 | 死守 M-1/M0/M1，M2 之后按实际痛感取舍 |

---

## 附：参考

- tifa.cpp — https://github.com/KakaruHayate/tifa.cpp （G2P/PFML C++ 实现、`docs/dataset-workflow.md`、`AGENT.md`）
- MaxLabel — https://github.com/KakaruHayate/MaxLabel
- TIFA — https://github.com/openvpi/TIFA
- g2pflow / PFML — https://github.com/openvpi/g2pflow
- dataset-tools — https://github.com/openvpi/dataset-tools
- GPT-SoVITS — https://github.com/RVC-Boss/GPT-SoVITS
- split-lang — https://github.com/DoodleBears/split-lang
- Aegisub — https://github.com/TypesettingTools/Aegisub
- OpenUtau（锚定模型）— https://github.com/openutau/OpenUtau
- LabelVoice — https://github.com/openvpi/LabelVoice
- diffscope — https://github.com/openvpi/diffscope
- notepad4 / Scintilla — https://github.com/zufuliu/notepad4 · https://www.scintilla.org/
