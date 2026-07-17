# 地图套件设计：FMG/watabou 审计结论、聚落生成路线与渲染方案

版本 v1.1 · 2026-07-18（v1.0：初版；v1.1 依作者方向反馈重定位：AI-first 编辑与种群种子、按需物化、性能预算——新增 §1.6-1.8、§2.1⓪，重构 §4，调整 §6/§7）
证据基础：Fantasy-Map-Generator@a8a8a35（2026-07-17，注意：该仓库是含经济系统扩展的 TypeScript 分支，goods/markets/production/taxes 为其原创，上游 FMG 并无）与 TownGeneratorOS@7fbc87a（2019-04-07）全源码精读审计（FMG 176 个功能点逐一定性；TownGeneratorOS 另做整管线走读），加三路文献调研（学术街道生成 / 地形约束聚落生长 / watabou 生态与格式契约）。
原始审计记录（算法细节、常数、逐文件笔记）见姊妹文档 **docs/FMG_AUDIT.md**；本文档只做决策与设计。

---

## 0. 审计总结论（TL;DR）

**功能点统计**：176 个 FMG 功能点 → 75 adopt（直接借鉴）/ 75 adapt（改造到球面 cell graph）/ 14 already_better（我们已更强）/ 12 skip。

**三条战略结论：**

1. **FMG 的物理不值得抄，FMG 的"编辑语法"和"叙事生成器"全部值得抄。**
   地形/气候/水文/河流我们已全面强于它（物理模拟 vs 启发式）。但 FMG 沉淀了数年用户校准的两笔资产：
   - **编辑 UX 语法**：staging→Apply/Cancel、lock/pin/group 三正交语义、Erase/Keep/Risk 破坏性合同、表格总览范式、派生字段/作者旋钮分离——与我们 worldstore gen/user 双层 + 锁 + DAG 脏传播**一一同构**，等于 FMG 已替我们做完了这套模型的 UX 验证。
   - **叙事/语义生成器**：宗教谱系、外交战争级联+编年史、省份/殖民地、命名形态学、湖泊分类、聚落特征位、战斗模拟器——这些是小说套件的直接素材来源，FMG 有而我们全缺。

2. **用户直觉成立：地理约束下的种子生长优于几何模板，且有三重独立证据收敛**——
   (a) Emilien 2012（村庄生成开山作）从图形学论证分散聚落必须建模 settlement↔road 正反馈，模板范式不适用；
   (b) GDMC 竞赛六年人评数据：模板/蓝图先行类在地形适应维度系统性垫底，高分方案共性是"地形特征图 + 迭代放置 + 寻路连接"（历史最高分、四维全项第一的 2022 冠军即此路线）；
   (c) 考古学选址预测建模独立收敛到同一配方（suitability 场 + 累积成本可达性），可信到用于预测真实遗址。
   watabou 式几何模板仅存的优点是**美学与秒级速度**——用"分档保真"覆盖（见 §1.1、§2.5），不作主路线。

3. **watabou TownGeneratorOS 的价值在"拓扑与分配"，与 M15 的"几何精化"正交互补。**
   patch→ward→block 三层分解、rateLocation argmin 分配、城墙/城门拓扑、递归地块切分（联排房涌现）、街道=负空间——半天可抄完的城市骨架管线，直接嵌进我们的物理约束框架。

---

## 1. 全局架构决定

### 1.1 分档保真（同一节点，多档实现）

每个生成域提供 2-3 档实现，**同一 DAG 节点、同一参数契约、不同 fidelity 参数**：

| 档位 | 实现 | 用途 |
|---|---|---|
| sketch | FMG/watabou 式一遍过启发式（秒级） | React Flow 即时预览、随机世界、次要实体 |
| sim | 物理/增量模拟（分钟级） | 正典生成 |
| （占位） | MFCG 深链（零成本） | M15 就绪前的城市详情页 |

这同时解决"作者迭代要快"与"正典要真"。内容寻址缓存把档位当参数，切档即重算。

### 1.2 编辑语法三件套（全工作台统一）

从 FMG 五十余个编辑器中提炼、并与我们已有机制对齐的统一协议：

1. **staging→Apply/Cancel**：一切笔刷/批量编辑先落前端暂存层（override map / TypedArray 副本，每笔画快照入 undo 栈），Apply 才写 worldstore user 层并打脏 DAG 下游，Cancel 零成本丢弃。
2. **lock / pin / group 三正交语义**：lock=重生成豁免（civ.py 重算路径必须实现"锁定实体保 id、保 cell 归属、扩张时视作硬边界"协议）；pin=可见性白名单（作者聚焦当前写作场景）；group=批量样式作用域。
3. **Erase / Keep / Risk 破坏性合同**（地形编辑会话）：进入地形编辑先选模式——Erase=下游全失效重算；Keep=冻结下游（禁改海岸线拓扑的编辑）；Risk=下游重算但实体经**重锚定协议**恢复。把"改地基的代价"前置告知作者。

### 1.3 实体重锚定协议（worldstore 新增，M12 合并契约的缺失环节）

抄 FMG Resample.process 的语义：地形重算后，gen/user 实体做 center 重定位（极不可达点回退）、港口贴岸、失效判定（所在 cell ∈ 幸存集合）、人口按面积比缩放。另抄其地形编辑 Risk 模式恢复流程的"burg 所在格强制保陆"——FMG 作用于全部 burg，我们收窄为**锁定实体**才反向约束地形（新设计，非 FMG 原语义）。这是"作者改了地形，小说实体不失效"的关键机制。

### 1.4 联邦契约：聚落上下文 JSON

抄 FMG→MFCG 的模式并升级。定义 WorldEngine 的 settlement 深链/调用契约，两层：
- **标签层**（兼容 MFCG 参数名）：population/river/coast/sea 方向/citadel/walls/plaza/temple/shantytown/hub/greens/farms，seed=世界seed+实体id（稳定可重放）。特征位生成规则直接抄 FMG defineFeatures 的人口阈值启发式，进 civ.py gen 层，同时喂小说一致性检查（"有城墙的城"）。
- **几何层**（我们独有）：真实约束几何——海岸折线、河流折线（含 Leopold 宽度）、入城道路方向、局部 DEM patch。FMG 生态"城与世界对不齐"的根因是只传标签；我们能治。

同一契约三个消费者：MFCG 深链（降级）、watabou 草图档、M15 全量管线。教训条款：外部工具回写（FMG 竟允许 referrer 参数直接改数据）必须走带鉴权的显式 API。

### 1.5 确定性纪律

- 拒绝采样/重试类算法（watabou 整城重开、FMG 选点 50 次重试）改为**阶段级确定性 jitter，重试计数进 cache key**，否则内容寻址缓存被打破。
- 抄 FMG alterHeights 的思想：需要 tie-break 时用已有确定性场（离岸距离等）做 ε 扰动，**零 RNG**。
- civ.py 已是正确范式（`default_rng` 按实体种子派生、零全局随机态）——把它写成全局规范，约束一切新节点。

### 1.6 AI-first 编辑：意图→约束，不碰几何

- **主编辑通道** = 上下文选择 + 自然语言 → AI 编译为类型化 world-ops → staging changeset → diff 审批落地。这是 novelkit 既有的"AI 只写 staging、作者 merge"模型从手稿域扩展到世界域，不是新机制。
- **一切人类意图以约束/种子的形式进入生成，而不是以几何编辑绕过生成**：手摆一座城破坏因果一致性（城没有存在理由）；约束种子（"此 cell 必须出 rank-0 聚落，名字锁定"）逼算法围绕它重新合理化世界（道路改道、层级重排、腹地重算）。一致性由构造保证，而非事后 linter 抓。
- 认知校准：算法买到的不是"客观"而是**一致性与可解释性**（每个实体的因果链可询问——"这城为什么在这：渡口+betweenness+腹地承载力"）；种子/filter/权重才承载风格与意图。质量瓶颈因此在种群包与约束的打磨环节——那是产品的核心交互，不是绕过人的地方。
- **写作即锁定**：手稿引用实体 → 晋升正典（锁）→ reseed 不可侵犯。reseed 永远有作用域（区域/实体子树/图层——语义上等于"重掷一切尚未被手稿引用的东西"）。世界自由度随故事推进单调收缩。
- **意图分类学**（world-ops 词表的规格）：世界公理（种群/气候/风带旋钮）· 放置约束（must-exist / must-not / 范围绑定）· 锁与属性覆盖（命名=作者声音）· 风格包 · 作用域 reseed。
- **操作层三硬件**：① 类型化词表 `define_species / set_filter / place_constraint / lock / override_attr / set_style / reseed(scope)`——AI 只能说这些动词，不能任意写数据；② dry-run diff：每个 op 先算 delta（增/删/移实体清单 + 失效锥范围预告）→ 作者审批才落地；③ 全程 provenance：prompt+参数 diff 入 changeset，"为什么有这个"永远可答。

### 1.7 按需物化（lazy materialization）

- 开局只算**基础层**：L0 地形/气候 + L1 聚落网络/领土/路网/命名（点实体+特征位）。这已足以支撑写作所需的一致性基建（travel-time / news / 实体名录）——以"生成完世界、开始写作"的视角，有基础信息就够。
- L2 村镇布局、L3 街区、POI 文案、AI prose（种群描述、城市风物）在**首次被写作/查询触达时**才物化，物化即入内容寻址缓存（DAG 天然支持 compute-on-demand）；Weber 纪元步进只为被触达的城市付费，其余 Krecklau 插值或不物化。
- **时间无关性不变量：何时生成不影响生成什么。** 纪律：seed=世界seed+实体id 严格派生；物化只读上游正典数据，不得反写兄弟实体。违反即漂移（先物化与后物化的世界不同，缓存与正典一起失效）——每个物化节点过此审查。
- 物化 → 被手稿引用 → 锁定（接 §1.6 写作即锁定），三态构成实体生命周期：潜在（只有 seed）→ 已物化（可 reseed）→ 正典（锁）。

### 1.8 性能预算（Azgaar 的反面教训）

FMG 编辑器卡顿是**结构性**的：单棵数万节点 SVG DOM + 主线程全量重算（审计 render_notes 里 innerHTML 拼接/合并 path/惰性 COA 那些技巧，全是在给这个架构续命）。我们只抄它的语义（lock/staging/表格范式），**不抄任何交互实现**（jQuery 对话框、整数模式码状态机——"怪诞感"的来源）。硬预算：

- 相机/球面交互恒 60fps：矢量层走 typed array + InstancedMesh + 纹理，零 DOM；实体表格虚拟化。
- staging 编辑纯前端（override map），感知延迟 <100ms；一切重算异步（SSE 进度 + 可取消 + 失效锥范围预告），永不阻塞画布。
- 常见 seeding 编辑 <1s 返回 diff 预览；L1 增量锥体重算秒级、全量分钟级（N=2048）。达不到就降档（预览用 sketch 档，正典档后台跑）。

---

## 2. 聚落生成设计（核心章节）

三层尺度，全部落在已有 cell graph + geoquery + worldstore 上。

### 2.1 L1 区域聚落网络（cell 原生 ~3.9 km，civ.py v2）

替换现有 civ.py v1 的"评分排序+贪心间距"一遍过。

**⓪ 种群注册表（species packs——L1 的新第一公民）**
种群 = worldstore 实体：`{描述 prose（AI 在线生成，gen 层字段）, suitability filter（受限 CellField DSL）, 通行成本 delta, 承载力模型, L2/L3 风格包, namesbase 语料, 种际关系先验}`。作者用自然语言描述（"矮人追寻矿藏，居于群山峡谷，构筑洞窟堡垒"），AI 编译成 filter——**词表受限**于我们的场（elevation/slope/Köppen/矿藏/river Q/离海距离/travel-time…），硬校验拒绝越界代码，命中计数 lint（选中 0 格或 4000 万格→诊断回喂 AI 自修）。**AI 在管线外产参数，管线内零 AI**：filter 是数据，进内容寻址缓存，同 seed 永远同世界。
L1 种子按种群分别采样（各用各的 S 场实例），扩张成本按种群 delta 叠加；**接触带**（多种群 suitability 均高的边界——丘陵、山麓、河谷口）作为显式派生输出：混居镇/贸易点/冲突区，小说素材富矿。种群是文化之上的维度（一个种群可承载多个文化，§2.2 的文化机制在种群内运行）。

**① suitability 作为 DAG 节点**
`S = 硬否决 × max(0, Σ wᵢ·fᵢ)`（Emilien 形式，fᵢ∈[-1,1]，任一硬否决项=-1 则 S=0）。项：
- 河流（按 discharge 分级，我们有真 Q）、海港（避风判定——补 FMG 的 haven/harbor coastal-adjacency pass；"每水体≥2港否则0港"约束）
- 坡度（钟形偏好）、农业潜力（Köppen+温度降水）、洪泛缓冲（河宽 w=7√Q 外推）
- 淡水湖+30 > 安全港+20 > 河口+15 的 FMG 相对权重作初始标定（数年用户校准的先验）
- 后期加**路网 betweenness centrality**（考古学证据：十字路口长出市镇，叙事价值极高）

**② Emilien 循环宏观化**（settlement↔road 正反馈，这是与 v1 的本质区别）
按 worldstore 纪元分批：
```
每纪元:
  放种子批（随机采样 × 按 S 概率 aggregation test）
  → 每 seed 立即以各向异性成本 A* 接入路网
     （坡度取方向导数而非标量；跨河=桥/渡口高固定成本，定价挂钩河宽 w=7√Q；
       已有路段成本 × wex≪1 复用折扣——这是长出干道树而非星形网的关键）
  → 网络距离回写 accessibility 场 → 影响下一批
```

**③ 层级与人口（不做历史模拟，用三条硬理论）**
- **Christaller 改造版**：在 travel-time 度量上做 spacing 晋升（半径 Rₖ 内无更高级中心则晋升 k 级）——六边形被山脉/河谷自动扭曲成可信形状；
- **Zipf rank-size** 定区域人口分布；**Bettencourt A∝N^(5/6)** 定 footprint/城墙周长预算；
- **重力模型** Tᵢⱼ∝PᵢPⱼ/f(travel-time)：累计边流量→道路分级（Galin 2011 路径合并成 junction，干道更怕坡、村道无所谓），同时直接加权 news_arrival（大城间消息更快）。
- 标度律与中心地半径**同时注册为 manuscript 一致性审计规则**（人口-面积-赶集城镇矛盾报警）——聚落层直接服务小说主线。

**④ 可选：SLEUTH-lite 节点**（spread/road-gravity/slope-resistance 三旋钮，砍掉校准）：每纪元几步 CA 长聚落 footprint，产出老城/新区/沿路带状发展的历史层次。

**⑤ sketch 档**：FMG 一遍过启发式（suitability 排序+间距递减重试+人口直映射）做秒级预览档。

**⑥ geoquery 多模态升级（水运，本批基建）**：陆/水双状态 Dijkstra（stateId=cell×2+isWater，换乘仅限港口 cell 且高惩罚——抄 FMG 贸易寻路）；水路跳步必须沿**河道邻接表**而非空间邻接（分水岭不变量——cell 相邻≠水文连通，FMG PRD 点名的坑，我们的球面寻路会犯一模一样的错）；"最终泄水体"分组给聚落打 port 属性（湖港解析到下游干流）；顺流/逆流成本不对称（FMG 没做，我们做——travel-time 获得方向性）。travel-time/news_arrival 全线受益，Christaller 层级、消息动画、旅程剖面都建立其上。

### 2.2 L1.5：文化与政体领土（与 L1 同批）

civ v1 的"成本距离 Voronoi"是本节的退化形式。升级为 FMG 的 **cost-field 生长**方案（成本表是数年用户调参资产），在球面 cell graph 上重标定。种群维度在其上：扩张在同种群空间内进行，跨种群边界走接触带语义（§2.1⓪）：

- **文化**：placeCenter 间距递减+偏置采样选祖地（球面用单位向量点积做间距检查）；文化预设集的选址 fitness 换成我们的场（温度/Köppen/离海距离/河流 Q），odd 拒绝采样保留为"该类文化出现概率"旋钮；Dijkstra 洪泛扩张，成本=距离+高程档+生物群系差异+过水×cell 面积（大水体自动比海峡贵）；只有 pop>0 的 cell 被赋文化——荒漠/冰原天然留白成 Wildlands。
- **政体**：同一算法、不同常数组——cultureCost 同文化 -9 / 异文化 100 的机制照抄（国界自动贴文化界，文化界又来自地理成本，三层因果链）；expansionism 作为一级作者旋钮（改完即时重算，"拖动祖地→领土实时重长"交互）。
- **后处理**：normalize 多数投票去锯齿（作者可开关）；assignColors 贪心图着色+抖动；极不可达点（cell graph 内部距离变换 argmax）作标签/纹章锚点——§3.1 区域标签的先决条件。
- **锁定协议在此落地**（§1.2）：锁定文化/政体重算时保 id、保领土、扩张视其为硬边界；"recalculate 只重跑扩张、保留命名身份"对应 DAG 节点粒度拆分（扩张节点失效、命名节点缓存命中）。

### 2.3 L2 村镇布局（局部切平面，米级 DEM patch，新模块）

**前置声明**：现有 M11.5 精化（refine.py）是 equirect 窗口精化，只到 ~100-400 m 档；L2/L3 需要的**米级切平面 DEM patch（gnomonic 重投影 + 再精化到 1-10 m）是 D 期第一个工作项**，不是既有能力——工作量已计入 §6。

行星上 95% 的聚落是村镇，不值得上张量场——这一层是 **Emilien 2012 近乎照搬**：
- per-building-type 兴趣图七项：sociability（吸引-排斥距离带）、worship、accessibility（动态，随修路更新）、slope、water、fortification、geographical domination（教堂/城堡选制高点）；
- 逐栋放置→立即接路（含锥角 θ 内找近路节点的 cycle 生成）→兴趣图回写；
- anisotropic conquest 地块分割（与真实村落地块统计吻合：邻居数 2.87 vs 2.81）；
- **growth scenario 事件流**（防御型→农耕型转换）与 worldstore 纪元/事件天然对齐——本身就是小说素材；
- 150 参数问题：3-5 个预设 village type + 兴趣场彩色 overlay 可视化调参。

**并入 GDMC 冠军三技巧**：城墙=建成区凸包外推+地形加权 A* 走线+平滑；功能分化用竞标而非预先分区；放置可撤销（高优先建筑可拆让位）。

**并入 watabou 移植件**（平面几何在切平面原样可用）：
- `rateLocation` argmin ward 分配微模式（每 ward 类型一个评分函数，逐个出队取最优——评分项换成我们的可达性/地价/坡度）；
- `findCircumference` 外轮廓行走 + 选门逻辑（**判据反转**：我们由路网∩墙轮廓定门，再用它"劈开门外 patch 造出路廊"的手法）；桥=水上的城门，同一抽象；
- `optimizeJunctions` 顶点焊接（阈值与街宽联动）；
- `filterOutskirts` 带状城郊衰减——populated-edge 距离换成 travel-time 场，得到沿官道 ribbon development，且能喂 news 模型。

### 2.4 L3 城市街区（M15 修订版）

坐标框架：**聚落种子 cell 为原点做 gnomonic 局部投影**，≤10 km 尺度球面问题消失；城市几何以局部米制坐标存储，回投影只在 MVT 切片时发生。

1. **入城骨架**：Galin 2010 各向异性最短路在放大 DEM 上生成城门-城际连接（桥按 w=7√Q 定价）；这些路径+矢量河流+海岸线**自动播种张量场 boundary basis field**，DEM 梯度播种 height field（街道沿等高线→山城肌理），市场/城堡种子放 radial field。作者以自然语言/约束追加或修改 basis field（AI 编译为 set_style/place_constraint op，存 worldstore user 层；手动笔刷降为专家兜底、后置）。
2. **生长循环（架构级决定：Weber 2009 纪元步进，非一次性生成）**：每 era 从现有网络向人口热力高兴趣区扩张 streamline；合法化=Parish & Müller localConstraints（snap/prune/坡度旋转，抛弃 L-system 外壳）+ Citygen 最小高差采样；实体持久 ID+有效期写 worldstore，作者锁定编辑在重模拟中幸存；次要城镇用 Krecklau 式关键帧插值省算力；且按 §1.7 按需物化——未被写作触达的城市根本不做 L3 物化。任意历史时刻可查询"当时的城门与街网"——travel-time/news 全时代成立。
3. **街区与宗地**：右手法则提面 → Clipper2 按道路等级宽度内缩（**街道=负空间**，watabou 洞见，MVT 不用单独出街道面层）；宗地双模式（Vanegas 2012）：新城/规则区 OBB 递归、老城/有机区 straight-skeleton 条带——**首版用 Clipper2 偏移近似 SS，坐实需求后 C++ 自写小型 Felkel（CGAL Straight_skeleton_2 是 GPLv3，禁止链接进内核；仅作离线质量基准）**。宗地持久 ID（编辑后重切保持对应）并入 worldstore。
4. **建筑脚印**：watabou Cutter 四件套照抄——createAlleys（gap 翻转涌现联排房）、createOrthoBuilding（L 形/十字复合体量）、ring（回廊/修道院）、radial（园林扇区）；(minSq, gridChaos, sizeChaos, emptyProb) 四参即风格，按 ward/纪元/文化配表。
5. **语义标注**：watabou/MFCG ward 分类学（铁匠区/贵族区/贫民窟出墙外）+ district 程序命名叠加在物理街网上——小说需要可引用的城区名词。
6. **明确 skip**：深度学习布局生成（不可确定重放、不可解释、训练分布=现代地球）；城内 agent 经济模拟（civ 层宏观已够，只抄"人口热力→dsep/宗地面积"接口）。

### 2.5 生成器分档汇总

| 聚落 | 正典档 | 快速档 | 占位 |
|---|---|---|---|
| 大城市 | L3 张量场+纪元步进 | watabou 草图 | MFCG 深链 |
| 村镇 | L2 Emilien | watabou 草图 | MFCG village 深链 |
| 次要聚落 | — | watabou 草图 | dwellings 深链 |

---

## 3. 渲染方案

### 3.1 球面矢量叠加层（当前最大缺口，最高优先）

现状：政体/水体/路网在球上没有渲染。方案全部落在现有 Three.js globe（/globe_mesh 顶点色管线）上：

- **图层模型**：采用 FMG 30 层 z-order 清单的裁剪版作为球面 overlay 顺序（ocean→terrain→hydro→admin(文化/政体/省份)→routes→symbols→labels→annotations）；样式=styleDoc JSON（layerId→props），内置主题+localStorage 自定义。
- **边界折线**：draw-borders 顶点链追踪移植到测地网格（顶点度=3 同 Voronoi，12 个五边形特判；"id 大者画边"去重约定照抄）→ 球面 polyline（Three.js Line2 或 shader 线）。civ.py 已有 dual-edge 提取可升级复用。
- **河流**：矢量河流（已有 Leopold 宽度+蜿蜒）→ 切平面内左右偏移 → 三角带；**流向动画**：R/G=sin/cos(弧长相位) 双通道纹理（双线性插值后 atan2 无缝解码），陡度信号做瀑布泡沫裙（非对称衰减 max(new, 0.55·steep)）；水面动画经 onBeforeCompile 注入（铁律：**运动只能来自相位，噪声必须静态**）。
- **路线**：沿河路段直接按 anchorIndices 从河流蜿蜒几何**切片借形**（相位匹配原则：两层几何要叠合必须共享同一参数化+相位，不各自近似）；贸易/消息动画=沿 polyline 移动 sprite，与 news_arrival 可视化共用。
- **聚落**：按 tier 分组 InstancedMesh billboard + 港口叠加副图标；标签=SDF 文本 billboard。
- **LOD 单点控制**（invokeActiveZooming 配方换算到相机距离）：标签可见窗口 [6px,60px]、纹章可读窗口 [25px,300px]、halo 宽度 ∝ 1/s^0.8、marker 近常屏幕尺寸、纹章可见才渲染（数千个零启动成本）。
- **区域标签**：极不可达点（cell graph 内部距离变换 argmax）+ 切平面射线投射选最优射线对 → 屏幕空间曲线文字；字号/换行/降级三段策略照抄。
- **符号防重叠**：确定性同步 forceCollide（预算 tick 数一次跑完，无动画帧）。

### 3.2 球面材质升级（把 FMG 启发式配方嫁接到真实物理场）

- **卫星纹理配方**：坡度阈值三段岩石、抖动阈值防等高线条带、雪聚沟壑、干热红岩、瑞士双色调晕渲、六类湖泊符号色——我们直接有真坡度/侵蚀/排水/温度/降水场，比 FMG 的伪装输入强一档；"单纹理多语义通道"（alpha=陆/湖/河/海动画带）设计照搬。
- **渲染期细节放大**（erosion-bake 移植，rhombus-atlas UV 空间）：3.9 km cell 到信标地面视角还差 2-3 个量级，这套是成熟补口——二值掩膜高斯模糊 0.5 等值线锚定（零高程严格贴矢量海岸线）、余弦核+坡度缩放相位 gully、enforceDownhillCourses 运行最小值压印（矢量河与放大地形对齐）、能量门用 primeval−eroded 高程差派生的净侵蚀近似（内核目前不导出侵蚀通量场，需补一个 cell 层导出；如需更准再加内核通量导出）。
- **海岸线分形放大**（矢量侧的同缺口补口）：粗糙度包络+中点位移，calm 段 B-spline / rough 段 Catmull-Rom 混合曲线路径，per-feature 确定性 seed（跨重绘稳定、内容寻址友好）；粗糙度包络由真实坡度驱动（陡崖粗糙、沉积岸平滑——FMG 只能随机包络，我们有数据）。近景缩放与 MVT 导出共用。
- 16bit 打包纹理必须 NearestFilter+手动双线性（硬件插值打包数据=灾难）。

### 3.3 2D 与导出

- **equirect/局部 SVG 导出**（小说插图/出版）：computed-style diff 内联 + 空组不动点删除 + 按引用裁剪 defs + 字体子集化 data-URI——FMG 导出管线的工程精华照抄。
- **等高带/等温线**：cell graph 上顶点链 marching（跨 rhombus 边界处理），固定色标保证多图可比；terracing 阴影做手绘风主题。
- **GeoJSON 分层导出**（geoquery 端点）：Cells/Routes/Rivers/Markers/Zones；**Rivers 属性名对齐 FMG 社区事实标准**（discharge/basin/sourceWidth/widthFactor）→ QGIS/Foundry VTT 生态零改动可用。
- **MVT 城市图**（MapLibre）：所有城市层（street rank/width/era、block、parcel、wall、gate、landmark）带有效期属性，时间轴滚动=按 era 过滤，零重切片；街网同时注册为 geoquery 城内可路由子图（城门节点接 travel-time/news）。
- **MFCG JSON 只写导出**（2-3 天）：GeoJSON FeatureCollection 方言（id 分层：values/earth/roads/walls/rivers/buildings/districts…，局部平面坐标）→ 免费白嫖 City Viewer 网页 3D、godot 插件等 watabou 下游生态。**只写不读、不作内部模型**（无语义无拓扑无地形）。
- **城市图默认视觉**：watabou 墨线+纸底+ward 淡彩+描边路（4 色阶 palette，形状而非颜色编码信息），MapLibre style 半天可复刻。
- **比例尺**：nice-number 算法 + 按视中心纬度真实地面分辨率（我们有真行星半径）。
- **纹章**：直接采用 **Armoria COA JSON schema** 作为 worldstore 纹章字段格式（MIT，可自托管 API），kinship/dominion 继承配合政体谱系（附庸国承父徽的叙事语义）；确定性同步布局防重叠。

---

## 4. 编辑与工作台：AI-first 编辑通道 + 兜底工具（v1.1 重定位）

**定位**：主通道是 §1.6 的"自然语言 → world-ops → staging diff → 审批"。人类直接操纵收缩为四类：锁/pin、属性覆盖（命名=作者声音）、seeding 手势（草图原语/约束放置）、diff 审查。下列 FMG 采纳项一律**取其语义、弃其交互实现**（§1.8）；标注"专家兜底"的项后置、按需实现。

- **world-ops API + staging-diff 审批 UI（C 期内核）**：操作词表与三硬件见 §1.6；filter lint（命中计数+分布摘要回喂 AI 自修）；每个 op 的 dry-run 输出=实体 delta+失效锥预告；审批 UI=diff 列表+球面高亮预览（复用实体浏览器联动）。

- **实体浏览器范式**（所有 worldstore kind 通用组件）：sortable 表头+模糊搜索+维度过滤+footer 聚合+百分比切换+CSV/txt 往返（导出全部地名批改再导回，diff 预览确认）+行内 zoom/edit/lock/delete+行↔球面 hover 联动高亮。
- **笔刷框架（专家兜底，后置）**：球冠盘=中心 cell+k 圈 BFS（k=半径/边长，不做精确测地筛选）；staging 层+每笔画快照 undo；提交期 diff 回滚实现 land/water 过滤。多数场景由"上下文选择+NL"覆盖，笔刷只为最后 5% 的精修保留。
- **地形编辑会话**：Erase/Keep/Risk 显式模式（§1.2）；9 种笔刷语义（Raise/Elevate/Lower/Depress/Align/Smooth/Disrupt+Fill 圆锥+Line 两点山脉）搬到球面，笔迹写入**作者增量场节点**而非覆盖模拟输出（重跑管线保留笔迹）；草图原语（Hill/Range/Strait…）以**物理半径 km 参数化**，从 N 解析推导衰减指数（不查表）；全局条件改写快捷操作（一行公式=一个 CellField 表达式节点），全局算子带陆地平移不变式 (h−sea)·k+sea 保海岸线；深洼地补湖/近海湖决口作者旋钮（lake_threshold/breach_limit，breach 记 provenance——"这里要有个大内海"的快速干预路径）；模板 DSL=React Flow 线性宏面板（可排序步骤、逐步快照调试、bypass 开关）；14 个 FMG 模板翻译成球面世界原型预设库+权重随机。
- **矢量覆盖层编辑（专家兜底，后置；语义先行）**：统一控制点协议（拖=移、点=删、点线段=插、约 200px 自适应密度），user 层存增量/变换而非覆写 gen 几何，重生成后重放。四个实例：河流（控制点拖拽→局部重跑蜿蜒+stream-burning；"点源头自动追踪+夺取/支流长度仲裁"的创建通道——正源之争语义即小说素材）、路线（拖拽端只改渲染 polyline、松手才提交 cell 拓扑）、海岸（顶点位移仅渲染层，明示"矢量编辑不回写高度"）、区域标签路径。
- **新建世界向导**：画廊=真实低配运行（N=64 跑板块+sketch 档出球面缩略图），seed 存卡片、所见即所得。
- **图像导入**：手绘 equirect→点图选色+自动色-高映射→写入作者场节点；PNG 功率曲线可调；真实地球 DEM 预设（"在真实地球上写小说"模式入口）。
- **参数面板标配"教学性活预览"**（固定 seed 玩具输入展示参数效果）——降低作者理解成本的最廉价手段。
- **专项编辑器优先级**（按小说套件价值排序）：① Notes/AI 扩写按钮（多厂商 AI 池即插即用）② Zones 事件区编辑器 ③ ElevationProfile 旅程剖面（travel-time 可视化，喂"第三日翻越垭口"类校验）④ Diplomacy 关系矩阵 ⑤ HierarchyTree 谱系树（React Flow 复刻，拖拽改父+防环）⑥ 世界统计面板（charts-overview 的维度/指标注册表直接映射 CellField，含政体气泡图/省份 treemap，CSV 喂数据核查）⑦ Namesbase 编辑器（语料+即时示例+质量评分闭环——教作者喂好语料）。
- **联动重算语法**：auto-apply 开关+显式 Recalculate 按钮；"重算与重生成分离"（recalculate=只重跑扩张保身份——对应 DAG 缓存粒度：扩张节点失效、命名节点命中）；破坏性操作确认+级联自愈 pass（抄 adjustProvinces：上层边界手改后下层自动分裂/换主）。
- **平台规范**：编辑模式中禁导出/存档（customization 旗标）；build-on-open/destroy-on-close；实体删除=墓碑（保 id 稳定）+"removed 不得持锁"；加载期引用完整性自修复清单（悬空引用降级+结构化日志，宽进严修）；worldstore schema 加 version 字段并现在就建 migrations/ 目录；深链协议 `/world/{id}?focus=cell:123&zoom=…`（手稿引用跳回地图——geoquery describe 反向链接的载体）+ 上次会话自动恢复；世界包 URL 直载（签名链接零后端分享）与多格式宽容加载；JSON 导出三档（Minimal 实体层/Region 局部窗口/Full atlas 二进制）；minimap 定位小球（第二 viewport 共享场景零复制）；driver.js 新手引导（导览驱动真实 UI、最多打扰 3 次）。

---

## 5. 叙事系统采纳清单（civ.py v2 非空间部分，全部来自 FMG 审计）

小说套件视角下 FMG 最被低估的部分——这些生成器产出的就是剧情种子：

1. **宗教层**（全套照搬，我们完全没有）：Folk/Organized/Cult/Heresy 类型学+"Old X"更名（正统 vs 异端叙事）；神名+教名双语法命名；route-aware 扩张（沿商路传播，与 news 共用图权重）；起源谱系 DAG。
2. **外交+战争**：关系矩阵生成、战争级联（含"盟友背叛"分支）、**自然语言编年史自动生成**——直接是 worldstore 事件实体+news 传播起点；国家增删并合/独立的关系变换表（8 行映射让每次独立事件的地缘后果都合理）。
3. **省份+殖民地**："陆路不可达→殖民地→New+母国名"（geoquery reachable 现成）；form[formName]+=10 行政命名自我强化（同国省份倾向同一称谓）。
4. **Zones 事件区**（11 类：战区/瘟疫/灾害…）：zone+起止纪元=事件历史；瘟疫沿路网 Dijkstra 传播与 news_arrival 同构。
5. **命名系统**：namesbase 马尔可夫链（单字母键+多字母伪音节，60 行高质量）+43 个语料文件直接搬运；国名形态学（Astellia 国/Astel 城/Astellian 人的词源一致性）；河流命名（河口文化取词根+支流尺寸档位通名）；特征分组命名学（内海/湾/群岛/盐湖……词表——geoquery describe 与地名类型学的基础，伪随机门换真实数据驱动）；路线命名（终点城市形容词+等级后缀）；政体形式三字段 name/formName/fullName + 文化→头衔映射表（"XX 苏丹国"的称谓一致性）；"id 取模"确定性抽样替代 RNG。
6. **聚落特征位**（citadel/walls/temple/shanty 人口阈值启发式）+文化类型 7 分类（游牧/高地/湖居…）——进 gen 层，同时是一致性检查事实源。
7. **湖泊水文分类**（补给 vs 蒸发→淡/咸/干+内流/外流）——一行公式换一个叙事维度（盐湖/干盐滩/里海式内流海）。前置：内核目前不导出蒸发场（蒸发只在湿度平流内部存在），需由 precip/runoff/PET 低成本导出一个 cell 层，或小改内核直接导出。
8. **军事生成 + 战斗模拟器**（成对实现，前者是后者的兵力数据来源）：征兵系数表+alert 战备公式+团编组，团实体入 worldstore（base/驻地二元坐标=补给线语义，crew/power 分离双口径）；战斗模拟器=阶段状态机+叙事判词数据化（11 档团状态/7 档战役结果）——输出即战报，几乎为 novelization 设计；掷骰与阶段留作者干预，结果写事件实体。
9. **经济层**（该 fork 原创，按需后置）：商品目录 DSL（改编译为 CellField 布尔表达式，**禁 new Function**）、市场经济流域（多源 Dijkstra）、事件溯源生产流水（deal 日志重放=纪元语义天然适配）、"经济按 burg 人口升序执行保护小城"等设计可整体搬运——但排在叙事层之后。
10. **审计规则注册**（生成即校验）：Bettencourt 人口-面积、Christaller 赶集从属、河流支流命名从属、"一国一都"类领域不变量。
11. **POI 标记程序化生成器**（审计认定 rendering 组产品价值最高项）：谓词式规则（谓词=我们的场：elevation/水力属性/路网/文化）+ min/each/multiplier 量控制 → worldstore gen 层 POI 实体，name+legend 文案喂地名库与一致性检查，lock=user 层覆盖；13 种 pin 形状+emoji 图标零资产成本；外部服务确定性嵌入（seed=世界seed+cell）。配套 markers 编辑器/总览走 §4 实体浏览器框架。
12. **冰层实体**：冰盖=CellField mask（喂 Köppen EF 一致性）；冰山=稀疏点实体（航线危险区——travel/news 的调味料）；两档温度阈值起步，gen 生成、user 拖拽增删。

---

## 6. 实施批次

依赖顺序与建议里程碑号（沿用 ROADMAP 编号，M15 已存在故城市生成记 M15′）：

| 批次 | 内容 | 依赖 | 量级 |
|---|---|---|---|
| **A（M16）球面矢量叠加层** | 边界/河流/路线/聚落/标签上球 + 图层模型 + LOD + 材质升级（卫星配方+河流相位动画） | 矢量几何已全备；卫星配方需补净侵蚀 cell 层导出（小改内核） | 2-3 周 |
| **B（M17）civ v2 = L1 聚落网络 + 领土 + 叙事层** | **种群包机制（§2.1⓪：filter DSL+AI 编译+注册表+lint）**+ suitability 节点 + Emilien 宏观循环（各向异性修路+wex）+ 文化/政体 cost-field 领土（§2.2）+ geoquery 多模态水运（§2.1⑥）+ Christaller/Zipf/Bettencourt/重力 + 港口 pass + §5 叙事清单（除军事 §5.8、经济 §5.9 外）+ 一致性审计规则 + sketch 档 | A（调参需要 overlay 可视化） | 5-6 周 |
| **C（M18）AI 编辑通道 + 编辑基建** | world-ops API+staging-diff 审批 UI+filter lint+实体浏览器+锁/重锚定协议+Erase/Keep/Risk 合同+模板宏/画廊/导入+专项编辑器前 3 项；笔刷/矢量控制点编辑后置为专家兜底 | A、B | 3-4 周 |
| **D（M15′）城市生成** | **米级切平面 DEM patch（第一工作项：精化金字塔扩到 1-10 m 档 + gnomonic 重投影）** → L2 Emilien 村镇 → L3 张量场+纪元步进 + 宗地双模式 + MVT + MFCG 导出 + Armoria | B + M11.5（现有仅 100-400 m 档，1-10 m 为本批新增工作） | 7-9 周（分两期：村镇先行） |
| 随批附带 | MFCG 深链占位（B 期 2-3 天）、GeoJSON 导出（B 期 3-5 天）、军事生成+战斗模拟器（§5.8，C/D 间隙 1-2 周）、SVG 导出/等高带/比例尺（A 期尾）、erosion-bake+海岸分形放大（A 后独立项，信标地面视角的前置） | | |

批次内先后可再议；A 在前是因为 B/C 的一切调参与编辑都需要"看得见"。

## 7. 风险与开放问题

1. **CGAL GPL**：Straight_skeleton_2 禁止链接进闭源内核——首版 Clipper2 近似，自写 Felkel 排 D 期后段；CGAL 仅离线基准。
2. **尺度换算**：FMG 常数标定在 1 万 cell 画布，我们 N=2048=4190 万 cell——所有密度/配额类常数（商品 4% 密度、市场间距、zones 数量）必须改为按面积 km²/大区配额，不能照抄比例。
3. **重试确定性**：watabou/FMG 大量拒绝采样，照抄会破坏内容寻址缓存（§1.5 方案待在 civ v2 首个节点上验证）。
4. **性能未证**：Emilien L1 宏观循环在 4190 万 cell 上的 A* 批量修路成本未实测（v1 的 dijkstra 已有基线，预计走陆地子集掩码+分区可控）；L3 纪元步进的增量失效粒度需要原型验证；米级 DEM patch（D 期第一工作项）的生成成本与缓存策略未定；§1.8 编辑通道预算（增量锥体秒级）未经原型验证。
5. **按需物化的时间无关性纪律**（§1.7）：必须保证"何时生成不影响生成什么"——seed 严格按 世界seed+实体id 派生、物化只读上游正典、不得反写兄弟实体；一旦违反，先物化与后物化的世界漂移，缓存与正典一起失效。每个物化节点过此审查（写进 code review checklist）。
6. **AI 编译 filter 的质量**：词表覆盖不足或语义误译会让种群分布违背描述——lint 闭环（命中数+分布摘要回喂）缓解，最终靠兴趣场 overlay 可视化+作者 diff 审批兜底。
7. **经济层范围**：§5.9 是该 fork 的原创扩展而非 FMG 主干，价值高但体量大——建议 D 期后单独立项评估，避免 B 期膨胀。
8. **外部回写信任边界**：MFCG referrer 教训——联邦契约只读深链，回写一律显式 API+鉴权。
