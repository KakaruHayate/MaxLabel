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

```bash
cmake -S . -B build -DMAXLABEL_BUILD_UI=OFF    # 只要 CLI
```

## 命令行

```bash
maxlabel_cli scan <dir>              # 列出目录里的段落、来源与 PFML 是否合法
maxlabel_cli show <dir> <id>         # 打印某段的 PFML
maxlabel_cli validate <file.pfml>    # 只做语法校验
maxlabel_cli set <dir> <id> [file]   # 写入 <id>.pfml（无 file 时读 stdin）
```

`scan` 会把 PFML 不合法的段落标出来并以非零码退出 —— 因为那些样本对齐器会静默跳过。

## 当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| M-1 | tifa.cpp：拆出 ggml-free 的 G2P 库、公开 API、补全 PFML 解析、实现 `to_pfml` | ✅ 已完成（tifa.cpp `feat/g2p-public-api`） |
| M0 | 数据层：目录扫描 / 导入 / 导出 / 校验闸门 + CLI | ✅ 已完成 |
| M1 | Qt6 GUI 骨架：段落列表 + PFML 编辑 + 自动提交 + 快捷键 | 🚧 骨架可用，待做语言切分与内联标注 |
| M2 | 音频面板（波形/频谱、选区播放、text-first 配对） | ⬜ 未开始 |
| M3 | 发音决策（候选弹窗、锚定、插入音素、非词汇音面板） | ⬜ 未开始 |

### GUI 现在能做什么

打开文件夹 → 左侧段落列表 → 中间编辑 PFML → `Ctrl+S` 保存。
`Z`/`X` 上一条/下一条，**切换前会自动提交**，不留 pending 编辑。
保存前会校验：不合法的片段不会被写入，并告诉你原因。

## 设计要点

- **不自己实现 PFML 语法**：一律操作 `G2PWord`/`G2PReading`/`G2PPath`/`G2PGroup`
  对象模型，序列化交给 `tifa_ggml::to_pfml()`，往返无损。
- **导出前必须校验**：对齐器对无效 PFML 没有回退，所以 `save()` 先 `validate_pfml()`，
  不合法就拒绝写入。
- **任何修改立即落盘**：不留 pending 状态，切段不会丢数据。
- **不做下游**：TIFA 输出侧的转换、切片修正都不在本项目范围内。

## 许可

MPL-2.0，与 tifa.cpp 一致。
