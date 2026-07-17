# Azgaar FMG + watabou TownGenerator 功能审计（附录）

> 审计对象：Azgaar's Fantasy-Map-Generator@a8a8a35（2026-07-17）与 watabou TownGeneratorOS@7fbc87a（2019-04-07）。审计日期：2026-07-18，由 10 个并行审计 agent 精读源码产出。本文档是原始审计记录，实现取舍见 docs/MAP_SUITE_DESIGN.md。

## 速览表

### terrain-core

| 模块组 | 功能 | 取舍 |
|---|---|---|
| terrain-core | 高度图模板 DSL 执行器 (Heightmap Template DSL / HeightmapModule.addStep+fromTemplate) | adapt |
| terrain-core | Hill/Pit 圆丘原语（BFS blob 扩散）(addHill/addPit) | adapt |
| terrain-core | Range/Trough 山脉/谷线原语（贪心脊线寻路 + 分层扩散 + prominence 支脉）(addRange/addTrough) | adapt |
| terrain-core | Strait 海峡原语 (addStrait) | adapt |
| terrain-core | 全局算子 Mask/Invert/Add/Multiply/Exponent/Smooth (modify/smooth/mask/invert) | adapt |
| terrain-core | blobPower/linePower 分辨率标定表 (getBlobPower/getLinePower) | adopt |
| terrain-core | 14 个内置模板 + 概率权重 (heightmap-templates.js) | adopt |
| terrain-core | 预制高度图 PNG 导入 (fromPrecreated + precreated-heightmaps.js) | adapt |
| terrain-core | Voronoi 构建（Delaunator 半边→双图）(voronoi.ts) | already_better |
| terrain-core | 抖动方格布点 + 边界伪点 + O(1) 定位 (graphUtils: placePoints/findGridCell/findGridAll/poissonDiscSampler) | already_better |
| terrain-core | 特征标记与距离场 (Features.markupGrid/markupPack + 分层 BFS markup) | adapt |
| terrain-core | 特征分组命名学 (Features.defineGroups) | adopt |
| terrain-core | 湖泊水文气候属性 + 封闭湖检测 (lakes.ts) | adapt |
| terrain-core | 地图重采样/子图提取 (Resample.process：投影反演 + 全实体恢复) | adapt |
| terrain-core | 高度图编辑三模式 erase/keep/risk + 下游再生管线 (heightmap-editor: showModeDialog/finalizeHeightmap/regenerateErasedData/restoreKeptData/restoreRiskedData) | adopt |
| terrain-core | 画笔系统：7 种连续笔刷 + 提交期过滤 (dragBrush/changeHeightForSelection/updateHeightmap) | adapt |
| terrain-core | Fill 圆锥填充笔刷 (applyFillBrush/collectFillSelection/applyConeToSelection) | adopt |
| terrain-core | Line 笔刷：两点山脉/峡谷 (placeLinearFeature) | adopt |
| terrain-core | 全局改写工具组：rescale/条件 rescale/一键平滑/扰动/清空 (rescale/rescaleWithCondition/smoothAllHeights/disruptAllHeights/startFromScratch) | adopt |
| terrain-core | 模板编辑器 UI (renderTemplateEditor/executeTemplate/addStep/downloadTemplate/uploadTemplate) | adapt |
| terrain-core | 图像转高度图转换器 (renderImageConverter/heightsFromImage/autoAssing/applyConversion) | adapt |
| terrain-core | 高度图选择画廊 (heightmap-selection.ts) | adopt |
| terrain-core | 海岸线分形化 (coastline-fractal.ts: 粗糙度包络 + 中点位移 + 混合曲线路径) | adopt |
| terrain-core | 海岸线设置对话框 (coastline-editor.ts) | adopt |
| terrain-core | 海岸线顶点编辑器 (coastline-vertex-editor.ts) | adapt |
| terrain-core | 浮雕图标编辑器 (relief-editor.ts) | skip |
| terrain-core | 高程剖面图 (elevation-profile.ts) | adopt |
| terrain-core | GPU 侵蚀细节烘焙 (erosion-bake.ts: 纹理构建+erosion shader+下坡后处理+内容哈希缓存) | adapt |
| terrain-core | 2D 等高层渲染器 (draw-heightmap.ts: isoband 提取 + terracing) | adapt |
| terrain-core | grid/pack 双图数据模型 + 全局管线 (PackedGraph.ts + data_model.md + generation_pipeline.md) | already_better |

### climate-hydro

| 模块组 | 功能 | 取舍 |
|---|---|---|
| climate-hydro | 纬度带温度模型 calculateTemperatures (public/main.js:946) | already_better |
| climate-hydro | 行进风带降水模型 generatePrecipitation (public/main.js:995) | already_better |
| climate-hydro | 生物群系矩阵 Biomes.getId (src/generators/biomes.ts) | adapt |
| climate-hydro | 河流水系生成 drainWater/flowDown (src/generators/river-generator.ts:60) | already_better |
| climate-hydro | 洼地填充 resolveDepressions + 高度扰动 alterHeights (river-generator.ts:296) | already_better |
| climate-hydro | 河宽/流量启发式 getOffset/getWidth (river-generator.ts:400) | already_better |
| climate-hydro | 蜿蜒插值 meander + 锐角松弛 (src/utils/pathUtils.ts:370) | adapt |
| climate-hydro | 河流多边形渲染 getRiverPath (river-generator.ts:425) | adapt |
| climate-hydro | 河流命名与类型 specify/getType (river-generator.ts:458) | adopt |
| climate-hydro | 湖泊气候数据与水量平衡 Lakes.defineClimateData/detectCloseLakes (src/generators/lakes.ts) | adopt |
| climate-hydro | 深洼地补湖与近海湖开口 addLakesInDeepDepressions/openNearSeaLakes (main.js:766/827) | adapt |
| climate-hydro | 冰川/冰山生成 Ice.generate (src/generators/ice-generator.ts) | adapt |
| climate-hydro | 等温线渲染 drawTemperature (src/renderers/draw-temperature.ts) | adapt |
| climate-hydro | 海洋深度分层渲染 OceanLayers (src/renderers/ocean-layers.ts) | skip |
| climate-hydro | 冰层渲染与增量重绘 draw-ice (src/renderers/draw-ice.ts) | skip |
| climate-hydro | 生物群系编辑器 biomes-editor (src/controllers/biomes-editor.ts) | adopt |
| climate-hydro | 河流编辑器 river-editor (src/controllers/river-editor.ts) | adapt |
| climate-hydro | 河流创建双通道 river-creator + addRiverOnClick (river-creator.ts / tools.js:737) | adopt |
| climate-hydro | 河流总览表 rivers-overview (src/controllers/rivers-overview.ts) | adopt |
| climate-hydro | 湖泊编辑器 lakes-editor (src/controllers/lakes-editor.ts) | adapt |
| climate-hydro | 冰编辑器 ice-editor (src/controllers/ice-editor.ts) | skip |
| climate-hydro | PRD：可航河流航线 navigable-river-routes (docs/prd) | adapt |
| climate-hydro | PRD：航线沿河蜿蜒渲染 meandering-river-routes (docs/prd) | adopt |

### peoples-polities

| 模块组 | 功能 | 取舍 |
|---|---|---|
| peoples-polities | 文化预设集与选址适应度函数 (culture sets + sort fitness) | adapt |
| peoples-polities | 文化中心放置 placeCenter（间距递减 + 偏置采样） | adopt |
| peoples-polities | 文化类型分类 defineCultureType（7 型启发式） | adopt |
| peoples-polities | expansionism 生成（竞争力参数） | adopt |
| peoples-polities | 文化扩张 cost-field 生长（Dijkstra 洪泛） | adapt |
| peoples-polities | 锁定实体重生成语义（lock & regenerate） | adapt |
| peoples-polities | 宗教生成全流程（Folk/Organized/Cult/Heresy） | adopt |
| peoples-polities | 宗教命名系统（神名 + 教名双语法） | adopt |
| peoples-polities | 宗教扩张（route-aware Dijkstra + 硬边界） | adapt |
| peoples-polities | 宗教起源树 defineOrigins（谱系生成） | adopt |
| peoples-polities | 国家创建 createStates（首都种子） | adapt |
| peoples-polities | 国家扩张 cost-field（与文化不同的常数组） | adapt |
| peoples-polities | 边界平滑 normalize（多数投票去锯齿） | adopt |
| peoples-polities | 极不可达点 getPoles（标签锚点） | adapt |
| peoples-polities | 政区配色 assignColors（贪心图着色 + 抖动） | adopt |
| peoples-polities | 战争史生成 generateCampaigns（假历史） | adopt |
| peoples-polities | 外交关系生成 generateDiplomacy（关系矩阵 + 战争级联） | adopt |
| peoples-polities | 政体形式 defineStateForms（等级 + 文化特化头衔） | adopt |
| peoples-polities | 税收与国库（salesTax/pollTax/treasury） | skip |
| peoples-polities | 省份生成（burg 种子 + 高程成本扩张） | adapt |
| peoples-polities | 野省/殖民地生成（连通性判定 + New X 命名） | adopt |
| peoples-polities | 事件区域框架 Zones（11 类生成器 + usedCells 互斥） | adopt |
| peoples-polities | Namesbase 马尔可夫链（音节级、单字母键） | adopt |
| peoples-polities | 国名形态学 getState（文化后缀语法） | adopt |
| peoples-polities | 表格总览编辑器范式（table overview pattern） | adapt |
| peoples-polities | 笔刷手动重指派（brush manual assignment + 暂存层） | adapt |
| peoples-polities | 中心拖拽实时重算（center drag → live recalculation） | adopt |
| peoples-polities | 人口编辑对话框（rural/urban 双通道） | adapt |
| peoples-polities | 国家增删并合与独立（add/remove/merge/independence） | adopt |
| peoples-polities | 手绘州界后的省份自愈 adjustProvinces（分裂/换主） | adopt |
| peoples-polities | 外交编辑器（关系视图 + 矩阵 + 编年史） | adopt |
| peoples-polities | Zones 编辑器（逐 cell 分解 + 擦除笔刷 + z 序） | adapt |
| peoples-polities | 谱系树编辑器 hierarchy-tree（origins DAG 可视化 + 拖拽改父） | adopt |
| peoples-polities | Namesbase 编辑器 + 质量分析器 | adopt |
| peoples-polities | 州标签曲线摆放（pole 射线投射 + 最优射线对） | adapt |
| peoples-polities | 州标签文字适配（字号/换行/降级三段策略） | adopt |
| peoples-polities | 州/省边界矢量提取 draw-borders（顶点链追踪） | adapt |
| peoples-polities | 州气泡图/省 treemap（比较图表） | adapt |
| peoples-polities | 文化 CSV 导入导出（往返编辑） | adapt |
| peoples-polities | 宗教编辑器特有交互（灭绝显隐/神名重生成/extent 切换） | adapt |
| peoples-polities | 国名编辑对话框（短名/形式/全名三段 + 生成辅助） | adapt |

### settlements-economy

| 模块组 | 功能 | 取舍 |
|---|---|---|
| settlements-economy | 首都放置 capital-placement | adapt |
| settlements-economy | 城镇放置 town-placement | adapt |
| settlements-economy | 港口判定与聚落微移 port-assignment + bank-shift | adopt |
| settlements-economy | 聚落人口模型 burg-population | adapt |
| settlements-economy | 聚落文化类型判定 burg-type getType | adopt |
| settlements-economy | 聚落特征与纹章 defineFeatures + defineEmblem | adopt |
| settlements-economy | 聚落分组系统 burg-groups | adopt |
| settlements-economy | Watabou 集成契约 watabou-links（city/village/dwelling 全参数） | adopt |
| settlements-economy | 路网生成主流程 route-generation（三层+Urquhart） | adapt |
| settlements-economy | 寻路成本函数 path-cost（陆/水全常数） | adopt |
| settlements-economy | 路线几何后处理 route-geometry（锐角平滑+沿河借形） | adapt |
| settlements-economy | 路线命名 route-naming | adopt |
| settlements-economy | 路网工具函数 route-utilities | adopt |
| settlements-economy | 商品目录与分布 DSL goods-catalogue | adopt |
| settlements-economy | 商品放置算法 goods-placement | adapt |
| settlements-economy | 市场创建与领地扩张 market-creation + expansion | adapt |
| settlements-economy | 市场定价与价格压力 market-pricing | adopt |
| settlements-economy | 全球市场间贸易 global-trade（套利重分配） | adapt |
| settlements-economy | 生产模拟 production-sim（worker loop + 递归配方规划器） | adopt |
| settlements-economy | 需求模型 demand-model | adopt |
| settlements-economy | 州税收与国库 taxes（文档 taxes.md） | adopt |
| settlements-economy | 军事生成 military-generation（战备率+征兵+团编组） | adapt |
| settlements-economy | 战斗模拟器 battle-simulator | adapt |
| settlements-economy | burg-editor 单聚落编辑器 | adopt |
| settlements-economy | burgs-overview 聚落总表 | adopt |
| settlements-economy | route 编辑器组（creator/editor/groups/overview） | adopt |
| settlements-economy | market 编辑器组（overview/单市场/deals） | adopt |
| settlements-economy | goods 编辑器组（目录/单品/分布可视化编辑器） | adopt |
| settlements-economy | production-chains 配方图可视化 | adapt |
| settlements-economy | production-overview 生产审计面板 | adopt |
| settlements-economy | compare-prices + trade-details 辅助面板 | adapt |
| settlements-economy | military/regiments 编辑器组（overview/单团/总表） | adapt |

### rendering-symbology

| 模块组 | 功能 | 取舍 |
|---|---|---|
| rendering-symbology | SVG 图层栈与样式预设 (layer stack + style presets, public/main.js + modules/ui/style-presets.js + layers.js) | adapt |
| rendering-symbology | 缩放感知 LOD 控制器 (invokeActiveZooming, main.js:540) | adopt |
| rendering-symbology | 聚落图标层 (draw-burg-icons.ts) | adapt |
| rendering-symbology | 聚落标签层 (draw-burg-labels.ts) | adapt |
| rendering-symbology | 纹章层与碰撞布局 (draw-emblems.ts) | adopt |
| rendering-symbology | COA 纹章生成器概述 (generators/emblems/generator.ts + 数据表) | adapt |
| rendering-symbology | 地物路径/海岸线渲染 (draw-features.ts) | already_better |
| rendering-symbology | 商品生产层 (draw-goods.ts) | adapt |
| rendering-symbology | 地图标记渲染 (draw-markers.ts) | adopt |
| rendering-symbology | 标记程序化生成器 (markers-generator.ts) | adopt |
| rendering-symbology | 市场区域层 (draw-markets.ts) | adapt |
| rendering-symbology | 军团符号层 (draw-military.ts) | skip |
| rendering-symbology | 地形符号层 (draw-relief-icons.ts) | adapt |
| rendering-symbology | 卫星纹理程序化烘焙 (draw-satellite-texture.ts::generateSatelliteTexture) | adapt |
| rendering-symbology | 河流流向相位纹理 (generateRiverFlowTexture) | adopt |
| rendering-symbology | 水面动画着色器注入 (view-3d-renderer.ts::applyWaterAnimation) | adopt |
| rendering-symbology | 3D 网格视图与侵蚀烘焙集成 (view-3d-renderer.ts 主体 + view-3d.ts 控制器 + 3d-view.md) | already_better |
| rendering-symbology | 比例尺 (draw-scalebar.ts) | adopt |
| rendering-symbology | 贸易动画：路径规划 (trade-animation.ts) | adopt |
| rendering-symbology | 贸易动画：绘制 (draw-trade-animation.ts + trade-animation-editor.ts) | adapt |
| rendering-symbology | 纹章编辑器 (emblems-editor.ts) | adapt |
| rendering-symbology | 标记编辑器 + 总览表 (markers-editor.ts + markers-overview.ts) | adopt |
| rendering-symbology | 标签编辑器：路径控制点系统 (labels-editor.ts) | adopt |
| rendering-symbology | 注记(Notes)编辑器 (notes-editor.ts) | already_better |
| rendering-symbology | 小地图 (minimap.ts) | adapt |
| rendering-symbology | 数据图表工作台 (charts-overview.ts) | adopt |

### app-platform

| 模块组 | 功能 | 取舍 |
|---|---|---|
| app-platform | 应用壳与完整生成管线调用顺序（generate() pipeline） | adapt |
| app-platform | 深洼地成湖与近海湖泊决口（addLakesInDeepDepressions / openNearSeaLakes） | already_better |
| app-platform | 温度模型（calculateTemperatures） | already_better |
| app-platform | 降水模型（generatePrecipitation）——1D 风带行进 | already_better |
| app-platform | reGraph 重打包（pack 网格生成） | skip |
| app-platform | 宜居度评分 rankCells（聚落选址前置） | adopt |
| app-platform | MFCG 城市生成器互操作（跨工具 URL 契约） | adapt |
| app-platform | 缩放系统：RAF 合并 + 语义缩放 invokeActiveZooming | adapt |
| app-platform | .map 存档格式（save.ts prepareMapData） | skip |
| app-platform | 加载管线与格式嗅探（load.ts uploadMap/parseLoadedResult） | adapt |
| app-platform | 版本自动迁移（auto-update.ts resolveVersionConflicts） | adopt |
| app-platform | 加载期数据完整性自修复（load.ts data integrity 段） | adopt |
| app-platform | Dropbox 云存储与共享链接（cloud.ts） | skip |
| app-platform | SVG 导出管线（export.ts getMapURL + inlineStyle + removeUnusedElements） | adapt |
| app-platform | 位图导出：PNG/JPEG/PNG 瓦片（export.ts） | skip |
| app-platform | GeoJSON 导出五件套（export.ts saveGeoJson*） | adopt |
| app-platform | JSON 数据导出四档（export-json.ts） | adapt |
| app-platform | 自动保存与保存提醒（autosave.ts） | adopt |
| app-platform | PWA 安装与离线（installation.ts + sw.js 注册） | skip |
| app-platform | 新手引导（ui-tour.ts，driver.js） | adopt |
| app-platform | 懒加载模块注册表（registry.ts + Controllers/Services 双桶） | skip |
| app-platform | 编辑器全清单（controllers/index.ts，51 项完整性兜底） | adapt |
| app-platform | 应用启动路由与会话恢复（checkLoadParameters/generateMapOnLoad） | adapt |
| app-platform | 架构文档与迁移策略（docs/architecture 五篇） | adapt |

## terrain-core（地形核心：高度图生成/模板 DSL/特征标记/湖泊/重采样/海岸线/地形编辑器）

### 编辑 UX 模式

FMG 地形编辑 UX 的核心是一份显式的『破坏性合同』：进入编辑先选 Erase（清空下游、退出时整链再生成）/Keep（锁死海岸线、数据全保）/Risk（可改海岸线、退出时按 grid 索引暂存-重铺恢复实体，含 burg 强制保陆、省/文化中心重定位）三模式，把『改地基的代价』前置给作者——这直接映射到 WorldEngine 的 DAG 脏传播+worldstore gen/user 双层与锁语义，是本组最该整体吸收的设计。操作层面：单选按钮状态机的笔刷面板（9 种笔刷：Raise/Elevate/Lower/Depress/Align/Smooth/Disrupt 连续拖刷 + Fill 单击圆锥填充 + Line 两点山脉/峡谷），半径/力度滑杆+光标圈预览，cellTypeFilter(all/land/water) 在提交期以 diff 回滚方式执行；撤销=整场 Uint8Array 快照栈（模板执行逐步入栈可单步回放）；实时反馈无处不在（改动格数 tip、陆地占比/均高统计、1px/cell 预览、3D 视图联动）。模板编辑器把生成过程暴露为可排序/可跳过/可存档的步骤表+种子框；画廊用真实低配运行出预览、seed 随卡片、所见即所得；图像转换器提供 手动色-高映射+点图选色+三种自动分配 的导入闭环。矢量微调（海岸顶点拖拽）被刻意定位为『渲染层修饰』并警示与高度真值的分离——生成真值与作者覆盖层的边界划得很清楚，与我们『物理模拟+作者可控编辑』的哲学同构，可直接当作 M7+ 工作台编辑会话的参考蓝本。

### 渲染架构笔记

FMG terrain-core 渲染是三层混合架构：① SVG 矢量主图——#viewbox 下按语义分组（#heights 编辑态、#terrs>#oceanHeights/#landHeights 常态、#coastline 按组、#debug 临时控件），编辑态用 d3 data-join 逐 cell polygon（id=cell{i}，mockHeightmapSelection 只增量更新触碰格），常态用 draw-heightmap 的『每高度值一条合并 path』isoband（cells 升序+顶点链行走+curveBasisClosed，skip/relax 降密，terracing=darker 副本 translate(.7,1.4) 伪 3D），海岸线是 coastline-fractal 输出的混合曲线 path（calm 段 Q 中点 B-spline / rough 段 Catmull-Rom C 命令）。② Canvas 2D 用作三类用途：1px=1cell 的灰度/配色预览（preview、drawHeights dataURL 进画廊 img）、图像导入的量化采样、以及 erosion-bake 里把 SVG path 经 Path2D 复用为光栅掩膜并用 blur（高斯 0.5 等值线技巧）、multiply/invert 合成（集合布尔）构建 GPU 输入纹理。③ WebGL（THREE RawShaderMaterial 全屏三角）做侵蚀细节烘焙：输出 16bit 打包高度+侵蚀信号+排水强度，readPixels 回 CPU 供网格与标签贴地采样（heightAt 双线性，不 raycast）。教学预览（粗糙度包络图/形状预览）是独立小 canvas，用固定 seed 玩具输入展示参数效果。对 WorldEngine：isoband 提取与海岸分形 path 构造器可直接迁到 2D 矢量叠加层/MVT 导出；Path2D 复用 SVG 几何做 GPU 掩膜、blur 当距离场近似这两招在我们烘焙管线同样适用。

### 功能明细

#### 高度图模板 DSL 执行器 (Heightmap Template DSL / HeightmapModule.addStep+fromTemplate)

**算法**：模板是纯文本，每行一步：`Tool a2 a3 a4 a5`，空格分隔。Tool∈{Hill,Pit,Range,Trough,Strait,Mask,Invert,Add,Multiply,Smooth}。执行前 Math.random=Alea(seed)（全局猴补丁保证确定性）。数值参数普遍是 "min-max" 字符串，getNumberInRange 支持小数（如 count=0.5 表示 50% 概率执行一次，用 P()）；位置参数是画布百分比区间 "a-b"，getPointInRange = rand(a%*len, b%*len)。分派：Hill/Pit/Range/Trough(count,height,x,y)、Strait(width,direction)、Mask(power)、Invert(prob,axes)、Add(v,range)/Multiply(f,range) 都走 modify()、Smooth(fr)。高度场是 Uint8Array [0,100]，海平面=20，lim() 截断到 [0,100]。

**数据模型**：heights: Uint8Array(len=points.length)；grid: {cells:{c 邻接表,h}, points, cellsX/Y, cellsDesired}；模板本体存于 public/config/heightmap-templates.js，可下载/上传为 txt（`Type count arg3 x y\r\n` 行格式）。

**编辑 UX**：模板即宏：一行一算子，人类可读、可分享（Cartography Assets 门户）、可设种子重放。

**渲染**：无直接渲染；执行后每步更新一次编辑历史快照。

**取舍**：**adapt** — 映射为 DAG 节点图里的『地形草图宏』：每个原语编译成一个 CellField(freq) 节点，输出不是最终高度而是隆升/目标高度增量，喂给板块+侵蚀物理管线做快速路径（跳过完整构造模拟时直接当高度）。文本 DSL 保留——作者可存档、可 seed 重放，与我们内容寻址缓存天然兼容。位置参数从画布百分比改成经纬度窗口或球冠参数。

#### Hill/Pit 圆丘原语（BFS blob 扩散）(addHill/addPit)

**算法**：Hill：h=lim(rand(height range))；选点最多重试 50 次直到 heights[start]+h≤90（避免叠爆）。change[start]=h，BFS：未访问邻居 change[c]=change[q]^blobPower*(0.9+0.2*rand)，change[c]>1 才入队；最后 heights=lim(h+change)。关键：change 是 Uint8Array，指数结果自动取整量化，这个量化就是 FMG 地形颗粒感的一部分。Pit：选点重试 50 次直到落在陆地(h≥20)；BFS 出队时 h=h^blobPower*(0.9+0.2rand)，h<1 停；对每个未用邻居 heights[c]-=h*(0.9+0.2rand)。注意 Pit 的衰减在『环』粒度（每次出队衰减一次），Hill 在『格』粒度（每条边衰减一次），所以 Pit 更陡。

**数据模型**：临时 change/used Uint8Array 与 heights 等长。

**编辑 UX**：同时是模板步骤与编辑器随机放置工具。

**取舍**：**adapt** — 在测地 cell graph 上原样可移植（BFS 邻接完全一致）。作为作者『图章』笔刷 + 模板算子。blobPower 需按我们 N=2048 的邻接环数重标定（见 blobPower 表条目）；量化噪声可以显式化成参数而不是靠 Uint8 副作用。

#### Range/Trough 山脉/谷线原语（贪心脊线寻路 + 分层扩散 + prominence 支脉）(addRange/addTrough)

**算法**：1) 端点：起点在 x/y 百分比窗口内；终点随机重试≤50次使曼哈顿距离∈[W/8, W/3]（Trough 上限 W/2；Trough 起点还要求 h≥20）。2) 脊线：从 start 贪心走向 end——在邻居中选『到终点平方欧氏距离』最小者，但以 randomness 概率（Range 默认 0.15，Trough 0.2，Line 笔刷可调 0~0.5）把某邻居的 diff 减半 → 路径蜿蜒；used 防回头，min==Infinity（死路）提前返回。3) 加高：queue=脊线全体，逐层 frontier 扩散；每层给 frontier 每格 h*(0.85+0.3rand)，然后 h=h^linePower-1，h<2 停（i 记录层数）。4) prominence 支脉：脊线上每第 6 个格子，重复 i 次：leastIndex 找最低邻居 min，令 heights[min]=(heights[cur]*2+heights[min])/3，cur=min 继续下坡——生成从脊线伸出的下降支脉/山嘴。Trough 全程做减法，其余同。

**数据模型**：同上，used Uint8Array。

**编辑 UX**：编辑器 Line 笔刷两点直连时直接传 startCellId/endCellId 绕过随机端点。

**取舍**：**adapt** — 贪心寻路在球面用大圆弦距平方替代欧氏平方即可。prominence 下坡支脉技巧值得偷——我们的物理侵蚀会自己长出支脉，但在『快速草图模式』和放大细节前的作者控制层，这是 O(ridge) 成本的廉价山体自然化。作为『画一条山脉』的作者手势→转成隆升带喂物理管线是最佳集成方式。

#### Strait 海峡原语 (addStrait)

**算法**：width=min(rand(width), cellsX/3)，width<1 时按概率 P(width) 跳过。vertical：start=(0.3~0.7W, 5)，end=(镜像 X±0.1W 抖动, H-5)；horizontal 对称。贪心寻路同 Range（0.2 概率减半）。然后逐圈下压 width 次：step=0.1/width，第 i 圈 exp=0.9-step*(width-i)（越靠中线指数越小压得越狠），heights[e]**=exp；溢出保护 `if h>100 then h=5`（0^负数之类的 hack 防御）。每圈把处理过的格并入下一圈边界（query 累积）。

**数据模型**：used Uint8Array 防重复。

**编辑 UX**：模板参数 w(宽度)+d(方向) 两项。

**取舍**：**adapt** — 『从画布边到边』的语义在球面无意义；改造成『两锚点之间的减高走廊』（作者点两点，或在两个板块/大陆特征之间自动选点）。指数下压剖面（中心低、边缘缓）比线性挖更像真实水道，保留。

#### 全局算子 Mask/Invert/Add/Multiply/Exponent/Smooth (modify/smooth/mask/invert)

**算法**：modify(range,add,mult,power)：range='land'→[20,100] 且启用陆地模式，'all'→[0,100]，否则 'a-b'。陆地模式下运算保持海岸线不变式：add 用 max(h+add,20) 钳位；mult/power 先平移 (h-20) 再运算再 +20。smooth(fr,add)：h'=lim((h*(fr-1)+mean(h,邻居)+add)/fr)，fr=1 为纯均值。mask(power)：nx,ny∈[-1,1] 画布归一化，distance=(1-nx²)(1-ny²)（中心1边缘0），power<0 反转；h'=lim((h*(fr-1)+h*distance)/fr)，fr=|power|——即『按 1/fr 强度向边缘压平』，用于防止陆地贴图边。invert(prob,axes)：以概率 P(prob) 按 x/y/xy 镜像，直接用方格索引重排 i=(y*cellsX+x) → nx=cellsX-x-1——依赖抖动方格网格的行主序布局。

**数据模型**：全部 map() 产生新 Uint8Array。

**编辑 UX**：Add/Multiply 的 to 下拉支持 all/land/interval（interval 弹 prompt 输 '17-20'）。

**取舍**：**adapt** — Add/Multiply/Exponent/Smooth 直接采纳为 CellField 一元节点（我们可能已有等价物；『陆地平移不变式 (h-20)*k+20』这个保海岸线技巧要抄）。Mask 的画布边语义在球面无意义→跳过或重定义为纬度带/距特征距离衰减。Invert 依赖方格索引，球面跳过（可用对跖/经度旋转替代实现『换个世界』的效果）。

#### blobPower/linePower 分辨率标定表 (getBlobPower/getLinePower)

**算法**：按 cellsDesired 查表：blobPower 1k→0.93, 2k→0.95, 5k→0.97, 10k→0.98(默认), 20k→0.99, 30k→0.991, 40k→0.993, 50k→0.994, 60k→0.995, 70k→0.9955, 80k→0.996, 90k→0.9964, 100k→0.9973；linePower 1k→0.75 … 10k→0.81(默认) … 100k→0.93。目的：Hill 的 change[c]=v^p 逐边衰减，p 越接近 1 衰减越慢；表让同一模板在不同 cell 数下产出『地图相对尺寸一致』的地貌（衰减到 1 所需环数 ∝ 分辨率线密度）。

**数据模型**：常量表，setGraph 时按 cellsDesired 选定。

**编辑 UX**：对用户透明——模板跨分辨率可复用的关键。

**取舍**：**adopt** — 思想直接采纳：任何『按环衰减』的草图算子必须以物理半径（km）为参数、从 N 推导指数，而不是硬编码。我们 N=2048（~3.9km 边长）时可解析求 p：值从 h 衰到 1 需 k 环，k≈半径/边长，p≈exp(ln(ln1.5/lnh)/k) 一类闭式，替代查表。

#### 14 个内置模板 + 概率权重 (heightmap-templates.js)

**算法**：volcano(3)/highIsland(19)/lowIsland(9)/continents(16)/archipelago(18)/atoll(1)/mediterranean(5)/peninsula(3)/pangea(5)/isthmus(2)/shattered(7)/taklamakan(1)/oldWorld(8)/fractious(3)，括号为随机抽取权重。可读性极高的『配方』范例：atoll=大丘+Multiply 0.2 压顶+小丘补礁；mediterranean=上下两条 Range+负 Mask(边缘保高中间压低)；isthmus=五组对角错位 Hill/Trough 链 + Invert 0.25 x；fractious=大量高丘+Mask -1.5(挖中心)+Mask 3(护边)+Add -20 30-100。每个模板 8~15 步。

**数据模型**：{id,name,template(string),probability}。

**编辑 UX**：新地图弹窗按权重随机，或画廊手选。

**取舍**：**adopt** — 直接移植为『世界原型预设库』：把百分比窗口翻译成球面经纬窗口/板块种子布局参数。权重抽取用于『随机世界』按钮。对我们更重要的是把这些配方当作板块生成器的目标形态先验（如 pangea→单超大陆聚合参数，archipelago→高海平面+碎板块）。

#### 预制高度图 PNG 导入 (fromPrecreated + precreated-heightmaps.js)

**算法**：23 张真实地球区域 PNG（africa-centric、europe、world、us-mainland…）。加载：建 cellsX×cellsY canvas（1 像素=1 cell），drawImage 缩放采样，逐 cell 读 R 通道 lightness=R/255，功率曲线 powered = l<0.2 ? l : 0.2+(l-0.2)^0.8（暗部线性=海洋原样、亮部压缩=抬升中低地），h=clamp(floor(powered*100),0,100)。

**数据模型**：PNG 文件 + {id,name} 清单。

**编辑 UX**：画廊里与模板并列，选中即所有地图共享同一形状（只有 voronoi 抖动不同）。

**取舍**：**adapt** — 对应我们的 equirect 导入：PNG→球面 IGM 采样（rhombus-atlas 每 cell 反查经纬→双线性取样）。功率曲线保留为可调导入参数。可以额外提供真实地球 DEM 预设作为『在真实地球上写小说』模式的入口。

#### Voronoi 构建（Delaunator 半边→双图）(voronoi.ts)

**算法**：遍历每条半边 e：p=triangles[nextHalfedge(e)]，若 p<pointsN 且 cells.c[p] 未建：edgesAroundPoint(e)（沿 halfedges[nextHalfedge] 环游，硬上限 20 防死循环）；cells.v[p]=各边所在三角形 id（三角形=Voronoi 顶点），cells.c[p]=各边起点过滤 <pointsN（去掉边界伪点），cells.b[p]= edges 数>邻居数 即边界格。三角形 t 首次遇到时：vertices.p[t]=外心（Math.floor 取整省内存），vertices.v[t]=经对向半边的相邻三角形，vertices.c[t]=三角形三点。nextHalfedge(e)= e%3==2 ? e-2 : e+1。

**数据模型**：cells:{v,c,b,i}, vertices:{p,v,c}（每 Voronoi 顶点恰 3 邻 cell / 3 邻顶点，边界顶点第三个为 -1）。

**编辑 UX**：无。

**取舍**：**already_better** — 我们是 goldberg 10N²+2 原生测地网格：拓扑固定、无运行时 Delaunay、rhombus-atlas O(1) 索引、无边界伪点问题。唯一值得记录的是 FMG 的 vertices.c 恒为 3——很多下游算法（connectVertices 边界行走）依赖这一性质；我们的网格顶点同样是 3 度（除 12 个五边形），移植那些算法时只需处理五边形特例。

#### 抖动方格布点 + 边界伪点 + O(1) 定位 (graphUtils: placePoints/findGridCell/findGridAll/poissonDiscSampler)

**算法**：spacing=rn(√(W*H/cellsDesired),2)；每格中心加 ±0.45*spacing 均匀抖动；边界伪点以 2*spacing 间距布在画布外扩 spacing 的矩形上用于裁剪。findGridCell：因抖动<半格，直接 floor(y/spacing)*cellsX+floor(x/spacing) O(1) 反查。findGridAll(x,y,r)：r/spacing 圈邻接 BFS 收集（近似圆盘）。findClosestCell：pack 用 d3 quadtree + WeakMap 按 cells.p 身份缓存。另有 mbostock 泊松盘采样器备用。

**数据模型**：grid{spacing,cellsX,cellsY,points,boundary}。

**编辑 UX**：笔刷圆盘选取即 findGridAll。

**取舍**：**already_better** — 球面无边界、rhombus-atlas 天然 O(1)。要抄的只有『笔刷圆盘=中心格+k 圈 BFS』这一实现（k=radius/边长），比球面精确测地距离筛选便宜得多，编辑器交互够用。

#### 特征标记与距离场 (Features.markupGrid/markupPack + 分层 BFS markup)

**算法**：洪泛：从 cell 0 起，land=h≥20 同类连通域打 featureId；type= land?island : border?ocean : lake（border=触画布边）。填充中同步标海岸：陆-水相邻→t[land]=1(LAND_COAST), t[water]=-1(WATER_COAST)；pack 版再标 LANDLOCKED=2。下一个种子用 featureIds.indexOf(0) 线性扫。分层 BFS markup()：从 start 值（陆 3、水 -2）逐层 ±1 推进直到 limit（水到 -10）→ 完整距海岸距离场 t。markupPack 额外：defineHaven——每个海岸陆格记 haven=最近水邻居（cell 中心平方距）、harbor=相邻水格数；特征周界 getFeatureVertices：找边界格→找相邻异类的起始顶点→connectVertices 沿顶点链环游；clipPoly 裁画布后 polygonArea 求面积；lake 若有向面积>0 反转顶点序，shoreline=周界顶点的陆地邻 cell 去重，height=Lakes.getHeight。markupGrid 开头重置 Math.random=Alea(seed) 保证 Erase 编辑模式重放一致。

**数据模型**：grid.features:{i,land,border,type}；pack.features 另有 cells 数、firstCell、vertices[]、area、shoreline[]、height、group；cells.t(Int8) 距离场、cells.f 特征 id、cells.haven、cells.harbor。

**编辑 UX**：无直接 UI，但支撑一切下游（港口、航线、湖）。

**取舍**：**adapt** — 球面 cell graph 上洪泛照搬（无 border 概念：ocean 判定改为『最大水域』或含指定极点水域）。haven/harbor 直接喂 civ.py 港口/聚落评分与 geoquery（『最近出海口』）。分层距离场 t 是廉价的『内陆度』，可作气候大陆性与 news 传播的输入。shoreline 顶点链是我们缺的 2D 矢量叠加层（海岸线折线）提取器。

#### 特征分组命名学 (Features.defineGroups)

**算法**：阈值全部取总格数分数：OCEAN_MIN=n/25、SEA_MIN=n/1000、CONTINENT_MIN=n/10、ISLAND_MIN=n/1000。ocean 类：>n/25→'ocean'，>n/1000→'sea'，否则 'gulf'。island 类：前一特征是 lake→'lake_island'；>n/10→'continent'；>n/1000→'island'；否则 'isle'。lake 类：temp<-3→'frozen'；h>60 且 cells<10 且 firstCell%10==0→'lava'（10% 伪随机门，免存 RNG）；无进出河：evap>4*flux→'dry'，cells<3 且 firstCell%10==0→'sinkhole'；无出河且 evap>flux→'salt'；默认 'freshwater'。

**数据模型**：feature.group: string。

**编辑 UX**：分组驱动样式（不同渲染组）与命名。

**取舍**：**adopt** — 直接采纳为 worldstore 特征实体的分类器 + geoquery describe 的词表（『内海』『湾』『盐湖』）。我们有真实温度/蒸发/水量，把 %10 伪随机门换成真实数据（lava→火山活动场，sinkhole→岩溶/坡度）；阈值改为面积 km² 而非格数分数。这是小说一致性检查需要的地名类型学基础。

#### 湖泊水文气候属性 + 封闭湖检测 (lakes.ts)

**算法**：getHeight=min(shoreline 高度)-0.1（湖面永远略低于最低岸）。defineClimateData：flux=Σ shoreline 对应 grid 格降水；temp=岸线均温（<6 格取 firstCell 温度）；蒸发用 Penman 近似：height_m=(h-18)^heightExponent，E=((700*(T+0.006*height))/50+75)/(80-T)，再 ×cells；outCell=最低岸格并登记 lakeOutCells。detectCloseLakes：MAX_ELEV=lake.height+ELEVATION_LIMIT(UI 滑杆)；从最低岸格 BFS，只走 h<MAX_ELEV 的格；若碰到水格且（是 ocean 或 该湖更高）→ 非深洼(closed=false)；否则 closed=true（河流生成时封闭湖无出流）。cleanupLakeData 在河流重算后删失效 inlets/outlet。

**数据模型**：feature 扩展字段：temp/flux/evaporation/outCell/closed/inlets[]/outlet/river/enteringFlux。

**编辑 UX**：lakeElevationLimit 是全局选项滑杆。

**取舍**：**adapt** — 我们的侵蚀物理已有洼地填充/溢出点（already better 于 BFS 检测），但『evap vs flux 决定盐湖/干湖』这套廉价分类判据值得采纳进湖泊实体属性（我们有真实蒸发场，直接替换 Penman 近似）。湖面=最低岸-δ 的不变式与我们 stream-burning 管线一致，可作校验断言。

#### 地图重采样/子图提取 (Resample.process：投影反演 + 全实体恢复)

**算法**：输入 projection/inverse/scale。父图 structuredClone 存档；河流先存 addMeandering 后的折线。新 grid 重生成后：每个新 grid 点 inverse→父坐标→父 pack quadtree 最近 cell→cells.g 反查父 grid cell，拷 h/temp/prec；scale≥2 时邻域均值平滑但钳型别（水 min(mean,19)/陆 max(mean,20) 保海岸拓扑）。重跑管线 phase 3-7（markupGrid、深洼加湖、开放近海湖、OceanLayers、温度、reGraph、markupPack、Ice）。restoreCellData：新 pack 陆格→父『陆格专用』quadtree 最近邻拷 biome/culture/state/religion/province/good；s、pop 按 areaRatio/scale 缩放（父面积为 0 时回退 1 防 NaN）。restoreRivers：投影已存蜿蜒点，出画布只保留一个越界点截断，重找 cells、conf 标汇流、widthFactor*=scale、重算 parent/basin/length。restoreCultures/Religions：幸存 id 集合校验，center=投影点在图内取之，否则用 getPolesOfInaccessibility(按 id 分区的极不可达点) 回退。restoreBurgs：population*=scale；落水→最近陆格；撞已有 burg→removed；港口经 getCloseToEdgePoint 重贴岸——取与 haven 格共享边两顶点中点，向其走 95%。restoreStates：军团重投影，出界删（连 notes）；neighbors 过滤。restoreRoutes：lineclip 裁剪折线。restoreZones：每父 zone 格→半径 √(父面积/π)*scale 的圆盘搜索并集。restoreFeatureDetails 按 firstCell 反查拷 group/name/height。经济只保 goods 目录+幸存市场再全量重算。

**数据模型**：ParentMapDefinition{grid,pack,notes} 深拷贝。

**编辑 UX**：供 transform-tool（原地变换）与 submap-tool（放大裁剪）调用，用户视角是『无损地放大一块区域继续编辑』。

**取舍**：**adapt** — 我们不需要重采样高度（多重网格原生多分辨率），但『实体重锚定协议』整套要抄进 worldstore：地形重算后 gen/user 实体的 center 重定位（极不可达点回退）、港口贴岸、实体失效判定（幸存 cell 集合∈validSet）、面积比缩放人口。这正是 M7 kernel 改动地形后保持小说实体一致性的缺失环节。

#### 高度图编辑三模式 erase/keep/risk + 下游再生管线 (heightmap-editor: showModeDialog/finalizeHeightmap/regenerateErasedData/restoreKeptData/restoreRiskedData)

**算法**：进入编辑先弹模式合同：Erase=清除全部次生数据、完成后从 phase3 起整链重生成（cultures/burgs/states/… 20+ 步，与 main.generate 相同序）；Keep=保留一切、禁止改海岸线（cellTypeFilter 锁 land，完成时仅把 grid.h 拷回 pack.h）；Risk=可改海岸线、尽量恢复数据：先把 pack 各场按 cells.g 折回 grid 索引暂存（biome/pop/routes/s/burg/state/province/culture/religion/good，未开侵蚀还存 fl/r/conf），有 burg 的格强制 h≥20 防淹城，culture 中心存 x,y，zone 存 grid 格集合；重跑水文气候+reGraph 后再按新 cells.g 铺回（只铺陆格；biome 对变动格重算），burg 重找最近陆格（淹没的非首都直接删，首都重定州中心），province 空了则删并从 state.provinces 摘除，zone 经 grid→pack 多重映射恢复；开了侵蚀则重跑 Rivers+defineGroups；经济按 goods 是否存在决定重算或全生成。finalize 校验：≥200 陆格、图像转换器须先退出。allowErosion 复选框控制 Rivers.generate 是否做侵蚀修饰（不允许时把 pack.h 回写为 grid.h 保持所见即所得）。

**数据模型**：customization 全局标志；storedLayers 记录进入前的图层开关用于恢复；edits 全局历史。

**编辑 UX**：这是 FMG 编辑哲学的核心合同：明确告诉作者『改地基 vs 保数据』的代价，Risk 模式明示可能出错。进入时强制切 heightmap 图层预设、放大动画的 Exit 按钮。

**取舍**：**adopt** — 对 WorldEngine 是最高价值的 UX 模式：三模式恰好映射到我们 DAG 脏传播 + worldstore gen/user 双层：Erase=下游节点全部失效重算；Keep=冻结下游（锁），只允许不改判据（海岸线↔流域拓扑）的编辑；Risk=下游重算但 user 层实体经重锚定协议恢复+锁定实体强制约束（burg 所在格强制为陆=我们的『实体地形约束』）。建议在 React 工作台把模式做成编辑会话的显式状态。

#### 画笔系统：7 种连续笔刷 + 提交期过滤 (dragBrush/changeHeightForSelection/updateHeightmap)

**算法**：拖拽起点 cell 固定为 start（Align 的参考）。每次 move：findGridAll(x,y,r) 圆盘选集，按 cellTypeFilter(all/land/water) 过滤后应用：Raise h+=power（水格直接跳 20 上岸，除非 water 过滤）；Elevate 按选集 BFS 序 d 用 interpolateRound(power→1) 渐变加（中心强边缘弱的软笔刷）；Lower/Depress 对称减；Align 全部设为 heights[start]；Smooth h'=rn((mean(同类邻居)+h*(10-power)+0.6)/(11-power),1)（power 越大越接近纯均值）；Disrupt h<15 不动，否则 h+=power/1.6-rand*power（期望略负偏的抖动）。limit=minmax(v, land?20:0, ocean?19:100) 保证过滤语义。拖拽结束 updateHeightmap：与历史栈顶 diff——land 过滤下任何『前值<20 或新值<20』的格回滚，water 过滤对称回滚（越界编辑在提交期整体撤销而非逐点禁止），tip 显示改动格数，然后 mock 重绘+入历史。radius 1-100 power 1-10，+/- 快捷键调半径。

**数据模型**：直接改 grid.cells.h；选区渲染走 mockHeightmapSelection 增量更新。

**编辑 UX**：按钮组单选状态机（pressed class），切换笔刷先 exitBrushMode 清理 drag 监听与光标圈；光标圈 SVG circle 跟随；Fill 隐藏半径行、Line 换 power/randomness 滑杆组。

**取舍**：**adapt** — 整组语义(Raise/Elevate/Lower/Depress/Align/Smooth/Disrupt)搬到球面 IGM 笔刷（Three.js 拾取→cell→k 环 BFS 盘）。『提交期 diff 回滚』代替逐点约束是好模式——与我们撤销/内容寻址快照兼容。注意我们的编辑应写入『作者增量场』节点而非直接覆盖模拟输出，这样重跑管线可保留笔迹。

#### Fill 圆锥填充笔刷 (applyFillBrush/collectFillSelection/applyConeToSelection)

**算法**：点击处 startHeight<20 → 水域填充模式，否则同高度陆地区域模式。洪泛选集：水模式收集连通 h<20；陆模式收集连通 h==targetHeight（严格等高）。守卫：选集<3 格报错；水域触及地图边界（开放海）拒绝。圆锥：先标边缘格（有选集外邻居），多源 BFS 算 edgeDistance；rise=max(1,round(power*10 * dist/maxDist))，newH=clamp(base+rise)，base=水填充?20:targetHeight——把封闭湖/平地填成中心最高的锥丘。

**数据模型**：inSelection/edgeDistance 临时数组，head 指针数组队列。

**编辑 UX**：单击即完成（非拖拽）；与 water 过滤互斥并给出明确错误文案。

**取舍**：**adopt** — 『把这个洼地/湖填成山丘』是作者常见诉求，多源 BFS 距离锥直接可用在 cell graph。可扩展为反向（把台地挖成盆地）。

#### Line 笔刷：两点山脉/峡谷 (placeLinearFeature)

**算法**：第一击放控制点（黄圈+橡皮筋线，data-cell 存起点格），第二击执行：power∈[-100,100]（0 报错），>0 调 addRange、<0 调 addTrough，直接传 fromCell/toCell；randomness 滑杆 0-100 映射 /200→0~0.5 减半概率（注释明示 >0.5 后排序趋稳所以 0.5 是最大蜿蜒）。执行在 HeightmapGenerator 副本上，diff 原场并按 cellTypeFilter 逐格接受，选区增量重绘+入历史。

**数据模型**：借用 #debug 层放控制点。

**编辑 UX**：橡皮筋预览线跟随鼠标（moveCursor 里更新 x2/y2）。

**取舍**：**adopt** — 两点画山脉/裂谷是必备作者手势；在球面上沿大圆取样贪心走。『在生成器副本上跑再 diff 合并』的模式适合我们：等价于临时节点求值+选择性合并进作者增量场。

#### 全局改写工具组：rescale/条件 rescale/一键平滑/扰动/清空 (rescale/rescaleWithCondition/smoothAllHeights/disruptAllHeights/startFromScratch)

**算法**：rescale 滑杆 -10..+10：全图 h+=v，land 过滤时 h<20 或 h+v<20 的格不动，water 过滤钳 ≤19；执行后滑杆自动归零（增量式）。条件 rescale：if h∈[X,Y] then h op V，op∈{×,÷,+,−,^}，全部转译为 HeightmapGenerator.modify(range,add,mult,power)（÷ 传 1/V，− 传 -V）；+/− 要求整数操作数。smoothAll=smooth(4,1.5)；disruptAll：h≥15 → h+2.5-rand*4；清空：全 0 并清空渲染（land/water 过滤下禁用，双击防呆）。

**数据模型**：无新增。

**编辑 UX**：rescale 面板与条件面板互斥展开；条件面板是『h≥X ≤Y ⇒ op V ▶』的一行公式 UI。

**取舍**：**adopt** — 『一行公式的条件改写』做成我们工作台的快捷操作（底层就是一个 CellField 表达式节点），比拉全节点图快。归零式增量滑杆是好交互。

#### 模板编辑器 UI (renderTemplateEditor/executeTemplate/addStep/downloadTemplate/uploadTemplate)

**算法**：步骤=DOM 行：type 徽章 + 跳过勾选（icon-check→半透明 opacity 0.5 即跳过）+ 删除 + 拖拽把手（jQuery sortable 纵向重排）+ 参数输入（Hill/Pit/Range/Trough: y,x,h,n；Strait: d,w；Invert: by,n；Mask/Smooth: f；Add/Multiply: to,v，to 支持 interval 弹 prompt 自定义区间并动态加 option）。执行：可选 seed 输入（空则 generateSeed），Math.random=aleaPRNG(seed)，清零高度场，逐步调用生成器并『每步压一次历史』→ 撤销可逐步回放模板执行过程。下载为 txt（跳过步不导出），上传按 5 段解析容错。templateSelect 切换预置模板时若有未保存改动弹确认。Enter 键=运行。

**数据模型**：模板状态就在 DOM（data-changed 脏标记），无中间模型。

**编辑 UX**：工具按钮行（H/P/R/T/S/M/I/+/*/~）点击追加步骤；种子框保证同模板同种子同结果。

**取舍**：**adapt** — 对应我们 React Flow 的『线性宏面板』：一个可排序步骤列表编译成节点链，逐步执行+逐步快照的调试体验值得照搬（我们的内容寻址缓存让『每步快照』免费）。跳过开关=节点 bypass。

#### 图像转高度图转换器 (renderImageConverter/heightsFromImage/autoAssing/applyConversion)

**算法**：上传图→graphWidth×graphHeight canvas→再降采样到 cellsX×cellsY→RgbQuant 量化到 ≤N 色（默认 100，可设 3-255）→每 cell 以量化色填充 polygon 并建未分配色票列表。手动：点色票（或直接点地图上的 cell→反查同色）选中该色所有 cell 高亮，再点 101 级高度调色板赋高（色板配色 i<20 用 (i-5)/100 拉开水色）。自动分配三法：lum——lab 亮度 l<13 判水 h=(l/13)*20，否则 h=l；hue——hsl 色相 >300 减 360，>170 判水 h=|hue-250|/3，否则陆 h=|hue-230|/3；scheme——与 FMG 导出配色精确匹配（先 indexOf 后最近色相）。同高度重复色票合并、按高度排序。Complete：从 polygon 的 data-height 写回 grid.h，未分配=0（海洋）；Cancel 回滚历史。overlay 透明度滑杆叠原图对照。

**数据模型**：颜色→高度映射在 DOM data-* 属性上。

**编辑 UX**：关闭时三选弹窗（继续/完成/放弃）；仅 Erase 模式可用。

**取舍**：**adapt** — 『作者手绘草图导入』工作流我们必须有：equirect 手绘→量化→色-高映射→写入作者场节点。『点地图选色』和自动 hue/lum 分配直接照搬；FMG-scheme 自动匹配对应我们导出配色的往返导入。

#### 高度图选择画廊 (heightmap-selection.ts)

**算法**：打开时对 14 个模板逐个真实运行生成器（共享 initialSeed，克隆 graph 删 h）生成预览：drawHeights 画 cellsX×cellsY 位图 dataURL 填 <img>；23 张预制图异步加载同管线。每卡片存 data-seed，↻ 按钮换新 seed 重跑该卡；全局选项（渲染海洋高度开关、配色方案下拉、全部重绘）触发 redrawAll——按各卡已存 seed 重放保持形状不变只换配色。Select 写 templateInput 并 lock('template')；New Map 用选中卡的 seed 直接 regeneratePrompt（预览即所得）。Edit Templates/Import Heightmap 按钮弹确认后带 tool 参数进 Erase 编辑模式。

**数据模型**：graph 缓存 + shouldRegenerateGrid 判断是否重建。

**编辑 UX**：响应式网格卡片（140px/80px/250px 三档），选中态高亮。

**取舍**：**adopt** — 『真实运行低配版生成器出预览、seed 存卡片、所选即所得』完全适配我们：用 N=64 的低频 CellField 跑板块+快速路径出球面缩略图（正好利用参数化频率类型）。这是新建世界向导的理想 UI。

#### 海岸线分形化 (coastline-fractal.ts: 粗糙度包络 + 中点位移 + 混合曲线路径)

**算法**：① makeRoughnessProfile：PROFILE_SIZE=256 的闭合包络，k=1..harmonics 个余弦谐波 amp=rand()、phase=rand*2π 叠加，归一化到 [0,1] 后取 contrast 次幂——本质是环上低频随机场，天然无缝；参数 harmonics 控制『粗糙区个数』（1=一大块，8=多小块）。② fractalize：按周长弧长参数化每个原顶点 t∈[0,1)；逐边递归 subdivideEdge：终止于 depth==0 或 len<minEdge 或 sampleProfile(midT(t0,t1))<smoothThreshold（calm 区完全不细分→留给平滑曲线）；位移=(rand-0.5)*√len*amplitude*roughness 沿法线（√len 是布朗式尺度不变），amplitude 每层 ×amplitudeDecay（Hurst）；midT 处理 0/1 接缝。湖岸 smoothThreshold×lakeSmoothThreshMult（默认 2，更平静）。种子 Alea(`${seed}_c${featureIndex}`) 每特征独立稳定。地图边界上的边（两端 x==0/W 或 y==0/H）跳过。默认参数：depth 4, amp 1.5, decay 0.9, minEdge 1, thresh 0.25, contrast 1.5, harmonics 4。③ buildCoastlinePath：按 origIndices 判断每跨段是否被细分；未细分（calm）段用 Q 中点 B-spline（≡curveBasisClosed，流畅弧线掩盖 Voronoi 棱角）；细分（rough）段对每个子段用向心 Catmull-Rom（切线 /8 ≈ tension 0.25）出 C 命令；两种曲线在中点/顶点间显式 L 衔接，起点在末段为 calm 时取中点保证闭环无缝。

**数据模型**：FractalizedShape{points, origIndices}；CoastlineSettings 8 参数+enabled。

**编辑 UX**：见海岸线设置对话框条目。

**渲染**：输出单条 SVG path d 字符串；erosion-bake 里同一路径经 Path2D 复用为 GPU 掩膜。

**取舍**：**adopt** — 这正是我们缺的『渲染期海岸线放大』：3.9km cell 提取的海岸折线在近距缩放/MVT 导出时用它加亚 cell 细节——确定性 per-feature seed 与我们内容寻址完美契合。球面改造：位移在局部切平面内做，法线取折线段大圆垂向；粗糙度包络思想可升级为由真实数据驱动（坡度陡=峭壁粗糙、沉积海岸平滑），FMG 用随机包络只是没有物理数据。混合曲线路径构造器（calm B-spline / rough Catmull-Rom）原样照搬。

#### 海岸线设置对话框 (coastline-editor.ts)

**算法**：8 个滑杆一一映射 CoastlineSettings（每个带独立 ↺ 重置）；5 预设 Default/Smooth/Rocky/Fjords/Archipelago（如 Fjords: decay 0.92+contrast 5.0+harmonics 2——高保持振幅+锐利分区=深切割少而深；Archipelago: harmonics 8+contrast 1=遍布小粗糙区）。每次 input 即改全局 defaultCoastSettings、重绘整图 drawFeatures 并刷新两个教学预览：粗糙度包络图（固定 PREVIEW_SEED 采样 256 点，阈值虚线分割 ROUGH 橙/CALM 青 双色带填充+描边，clip 到水平带实现）；形状预览（4 点菱形在画布尺度 fractalize，海洋径向渐变+陆地渐变+投影阴影，虚线叠原始骨架+顶点白点，禁用时压暗+OFF）。自绘 iOS 式开关联动禁用滑杆与预设。

**数据模型**：改的是模块级单例 defaultCoastSettings（非持久化）。

**编辑 UX**：参数→即时全图反馈 + 两个『解释参数含义』的活预览，滑杆 tooltip 写明物理意义（Hurst 指数、√len 缩放）。

**取舍**：**adopt** — 抄这个 UX 范式：任何生成参数面板配『教学性活预览』（在固定 seed 的玩具输入上展示参数效果），显著降低作者理解成本。React 工作台里做成节点检视器的标准组件。

#### 海岸线顶点编辑器 (coastline-vertex-editor.ts)

**算法**：点选海岸特征→在 #debug 层画周界所有 Voronoi 顶点（r=0.4 圆）+邻接 cell 多边形。d3.drag 拖顶点：直接改 pack.vertices.p[v]，实时重算 feature path d、polygonArea 面积显示、邻接 cell 多边形；dragend 时按开启图层重绘 states/provinces/borders/biomes/religions/cultures。tooltip 明示『仅微调用——真要改地形请编辑高度图』（矢量与 cell 高度会失配）。分组管理：coastline 下 <g> 即组；换组=appendChild；新建组（小写下划线化、查重、禁数字开头，旧组只剩 1 元素时就地改名）；删组把成员并回 sea_island；sea_island/lake_island 两默认组受保护。

**数据模型**：直接改共享 vertices.p——所有引用该顶点的 cell/特征同步变形。

**编辑 UX**：组=样式作用域（编样式按钮直达 Style Editor 对应组）。

**取舍**：**adapt** — 对应我们的『作者矢量覆盖层』：生成海岸线之上允许 user 层顶点位移（存 worldstore user 层带锁），渲染时应用、重生成时保留。『矢量编辑不回写高度』的明确警示文案值得保留。组=样式作用域的模式适用于我们未来 2D 叠加层样式系统。

#### 浮雕图标编辑器 (relief-editor.ts)

**算法**：三模式。Individual：所有 use 图标可拖（记录 grab 偏移）、换 href、改尺寸（中心保持：x-=Δ/2）、复制（-3,-3 级联偏移直到 x 无冲突）、raise/lower z、删除。Bulk add：拖拽刷，每 drag 事件尝试 ceil(r/10) 次随机极坐标点；quadtree 近邻 <spacing 拒绝、pack.cells.h<20（水面）拒绝；尺寸 h=size/2*(0.8+0.4rand)；z 序=painter 算法：维护各图标底边 y+2h 的有序 positions 数组，二分式线性找 nth 后 insert(':nth-child(nth)')——南边的图标后绘制盖住北边，无需全量重排。Bulk remove：按选中类型（或 Any）建图标中心 quadtree，拖拽半径圆盘删除。图标集 simple/colored/gray(-bw) 三套 SVG symbol。

**数据模型**：纯 SVG DOM（#terrain 下 use 元素），无数据模型——保存即序列化 SVG。

**编辑 UX**：模式切换重配光标/滑杆可见性；删除按类型批量带确认计数。

**取舍**：**skip** — 手摆 2D 装饰图标与我们物理渲染路线（globe 顶点色+未来信标地面视角）相悖；若日后做风格化 2D 导出主题再回头参考其 painter 序插入与 quadtree 间距技巧。生成侧的 biome 图标密度表（biomesData.iconsDensity）留作导出美术参考。

#### 高程剖面图 (elevation-profile.ts)

**算法**：输入 cells 序列+路线长+isRiver。河流单调化：比较首末格高度定 slope 方向，逐点钳制 h 不得逆坡（防渲染出上坡河）。水格高度取湖面高或 20。真实高度=(h-18)^heightExponent × 单位系数。burg 去重（连续同 burg 只标一次），末点若恰是 burg 把标签挪到最后。绘制：d3 线性 scale，5 种曲线（默认 MonotoneX，防插值过冲）；地形填充用 natural 配色在 [minH,maxH] 间 ≤20 个 stop 的纵向渐变（山顶色→谷色）；曲线+底边闭合成填充 path，另描 1.5px 棕色轮廓；下方 10px biome 色条逐格 rect，tooltip 拼 biome/省/国/宗教/文化/高度/人口；X 轴刻度换算距离单位 rn(d/(n-1)*routeLen)；burg 标签防重叠：greedy 上推——与已放标签 |Δx|<70px 且 |Δy|<14px 则上移 14px 直到无冲突（顶到 12px 为止），位移大时画带箭头 marker 的引线，曲线上加白底圆点；悬停 crosshair：竖虚线+跟随圆点+tooltip（距起点距离、海拔、biome、burg）；统计行显示总爬升↑/下降↓。导出 CSV（21 列含 lat/lon/人口/各政区色）、SVG、PNG（SVG→blob→img→canvas）。

**数据模型**：chartData{biome,burg,cell,height,points,min/max}。

**编辑 UX**：从路线/河流编辑器唤起的只读分析视图。

**取舍**：**adopt** — 与 geoquery route/travel-time 是天作之合：我们有真实米制高程、Köppen、行政层，直接产出『旅程剖面』喂小说一致性检查（『第三日翻越垭口』类描写校验）与手稿参考图。河流单调钳制我们不需要（水力学保证），但 burg 标签防重叠 greedy 和 biome 色条照搬。做成 FastAPI 端点+React 组件。

#### GPU 侵蚀细节烘焙 (erosion-bake.ts: 纹理构建+erosion shader+下坡后处理+内容哈希缓存)

**算法**：缓存键=FNV-1a 混入 seed/尺寸/参数+grid.h+pack.r+pack.fl 全数组。输入纹理三张：uHeight cellsX×cellsY（R=grid 高度，先用 pack 子格均值回填细节；G=最大陆邻高差×10=局部起伏度）双线性放大即基础地形；uCoast bake 分辨率（R=陆地掩膜高斯 blur(taperPx)——二值边缘高斯模糊后 0.5 等值线仍精确在原矢量海岸线上，shader 以此锚定零高程线；G=水面高度字节（海 20/湖 feature.height，先填充+6taperPx 描边膨胀再 blur）；B=河流真实几何多边形+每段中心线描边(宽度钳 ~1 texel 保上游连续)光栅化；A=湖 group 码×40 不 blur 保离散）；河口处理：河流∩(水域膨胀 mouthRadius=2taperPx 区) 经 multiply+invert 从陆掩膜抠除，让海岸线跟进河口。uRivers 1/4 分辨率谷地画布：Chaikin 平滑 2 次的河线按 4 个 pass 变宽变暗描（grow 1/2/3.4/5.2, gray 255/140/70/30，宽=(spacing*0.6+均宽*3)*grow），另加亚河流排水线（fl≥20 无河的格连向最低邻，gray 12+28*min(fl/100,1)）。Fragment shader（全屏三角，输出 RGBA=高度 16bit hi/lo + 侵蚀细节 ±0.4 归一 + 排水强度）：clayjohn/Fewes 余弦核侵蚀——4×4 抖动枢轴窗口，w=exp(-2d²)，phase=TAU·dot(d,dir) 用未归一化 dir → 条纹频率随坡度缩放，平地退化为常数（内建淡出、峰顶收尖）；6 octave 派生反馈转向（dir=梯度旋转 90°×DIR_SCALE 0.5，钳 4 条纹/格），runevision 符号技巧 sign(e.yz) 反馈让细 gully 锁定角度；边缘整形 e.x=1-2*((1-e.x)/2)^0.7 尖脊圆谷；平地 kernel≈+1 的抬升只保留给山峰段（hillCurve smoothstep(0.40,0.70)），其余混回；能量门=海拔曲线 max 局部起伏曲线(×海拔权重) × 坡度门 smoothstep(0.015,0.05)（保平原/台地顶光滑，地板 0.5）× 岸边淡入 (h-0.20)/0.10；unsharp 掩膜（1.5 格 4 抽头模糊差）×0.8×hillCurve 提脊线；河谷雕刻 depth=uRiverDepth*mask^1.4*mix(0.3,1,地形)×min(高出水面量,0.15)；最后 h=mix(waterSurface,max(h,ws),smoothstep(0.5,0.7,landMask)) 一步完成水面压平+岸线过渡。CPU 后处理 enforceDownhillCourses：沿每条河真实蜿蜒线 0.75-texel 步进，维护 bed=运行最小值，凡采样高于 bed+1e-4 的位置以圆盘压印回 bed：核心半径=河半宽+1，羽化=+clamp(depth*150,1.5,10) smoothstep 过渡，目标钳 ≥局部水面——保证渲染网格上河床严格单调下坡。heightAt() 双线性采样供标签/图标贴地（不 raycast 网格）。

**数据模型**：ErosionBakeResult{key,heights Float32,pixels RGBA,coast RGBA,cols,rows} 模块级单例缓存。

**编辑 UX**：参数 strength/riverDepth/octaves/bakeResolution 由 3D 视图设置暴露。

**渲染**：Canvas2D 合成（multiply/invert 做集合运算、blur 做距离场近似）→THREE DataTexture→RawShaderMaterial 全屏 pass→readPixels 回 CPU。

**取舍**：**adapt** — 我们的侵蚀是真物理，但 3.9km cell 到信标地面视角仍差 2-3 个量级——这套『渲染期外观侵蚀放大』恰是该缺口的成熟方案。直接偷：①二值掩膜高斯模糊 0.5 等值线锚定技巧（海岸零高程精确贴矢量线）；②余弦核+坡度缩放相位+符号反馈的 gully 生成（球面上在 rhombus 图集 UV 空间跑）；③enforceDownhillCourses 运行最小值压印（我们蜿蜒河渲染同样需要）；④FNV 内容键缓存与我们内容寻址同构。能量门可换成真实侵蚀通量场——我们比 FMG 多有物理数据可驱动它。

#### 2D 等高层渲染器 (draw-heightmap.ts: isoband 提取 + terracing)

**算法**：cells 按 h 升序排；currentLayer 以 skip 步长推进（skip=每几级合一层）；仅『存在更低邻居』的边界格触发描边：从该格找一个『邻接三格中有更低者』的顶点起，connectVertices 行走——每步在当前顶点 3 邻顶点中选『两侧 cell 的 h≥h 判定发生翻转』的方向（c0!=c1 等奇偶判据），途中把同高度 cell 标 used 防重复；MAX_ITERATIONS=顶点总数防死循环。relax 简化=每 (relax+1) 个顶点取 1。同一高度所有闭链拼进同一条 path（d 字符串累加），d3 line + 可配置闭曲线（默认 curveBasisClosed）。渲染 0..100：h=0 且开启海洋时铺 scheme(1) 底 rect，h=20 铺 scheme(0.8) 陆地底 rect；每高度一条 path 按配色填充，terracing>0 时先画 darker(terracing) 的阴影副本 translate(.7,1.4)——伪 3D 台地效果。海洋/陆地分组各自独立 skip/relax/curve/scheme/terracing 属性（读自 SVG 组 attr）。

**数据模型**：paths: (string|undefined)[101]，每高度最多一个 path 元素（带 data-height 供换色）。

**编辑 UX**：样式编辑器暴露 skip/relax/curve/terracing。

**渲染**：最多 202 个 path 元素渲染整张高程图——比逐 cell polygon 少两个数量级，是 FMG 2D 高程层性能的关键。

**取舍**：**adapt** — 等高带提取正是我们 2D 矢量叠加层/MVT 导出需要的：在球面 cell graph 上同样可行（顶点行走判据只依赖 3 度顶点，五边形 cell 12 个特判），输出按 rhombus 分块或投影后的等高带 polygon 喂 MapLibre。terracing 阴影是廉价的手绘风效果，导出主题可用。

#### grid/pack 双图数据模型 + 全局管线 (PackedGraph.ts + data_model.md + generation_pipeline.md)

**算法**：grid=抖动方格 Voronoi（气候/高度原始层，h/temp/prec/t/f）；pack=对陆地重打包优化的第二张 Voronoi（一切文明数据），cells.g 是 pack→grid 唯一映射。高度 Uint8 0-100 海平面 20；一切场都是 typed array；实体（culture/burg/state/province/religion）数组下标即 id、删除只打 removed 标记、lock 布尔控制再生成豁免。管线 16 phase（seed→grid+heightmap→水文→气候→repack→河流 biome→ice→goods→ranking cultures→聚落政治→省→命名→经济→军事→收尾），文档明确列出 3 个复制点（erase 出口、risk 出口、resample）与新增全局步骤的 6 步检查清单。

**数据模型**：见上；routes 用 Record<cell,Record<cell,routeId>> 邻接。

**编辑 UX**：removed+lock 语义贯穿所有编辑器：锁定实体不被任何再生成覆盖。

**取舍**：**already_better** — 我们 cell-native IGM+DAG 已优于双图 hack（pack repack 本质是海岸自适应密度，我们用 N+放大细节解决；他们 cells.g 单向映射造成大量恢复代码）。但两点要吸收：①lock/removed 的实体豁免语义与我们 worldstore gen/user+锁一致，FMG 验证了它对『编辑-再生成循环』够用；②『复制点清单』文档模式——我们每加一个 DAG 节点也该审计所有部分重算入口，写进 ROADMAP 的工程规范。

### 值得偷的技巧

- 全局猴补丁 Math.random=Alea(seed) 是 FMG 全部确定性的根基——连 Features.markupGrid 开头都重新播种，专门保证『Erase 模式编辑后重放与初次生成 bit 级一致』；模板编辑器则允许每次执行换种子。
- Hill 的扩散缓冲 change 是 Uint8Array：change[c]=v^p*(0.9+0.2r) 每步被隐式取整，这个量化噪声就是 FMG 山体的颗粒质感来源——不是 bug 是（无意的）特性，移植时要显式参数化。
- modify() 的陆地不变式：对 land 的乘法/幂运算全部在 (h-20) 平移域进行再 +20，加法用 max(h+add,20) 钳位——保证任何全局调整都不意外改写海岸线拓扑。
- blobPower 表（1k→0.93 … 100k→0.9973）让同一模板在任何 cell 数下产出地图相对尺寸一致的地貌——『草图算子必须以物理尺寸参数化』的反面教材式证明。
- Range 的 prominence 技巧：脊线每第 6 格向最低邻居连做 i 次 h_min=(2h_cur+h_min)/3 下坡行走，O(脊长) 成本长出天然山嘴/支脉。
- 海岸分形的粗糙度包络：环上余弦谐波和（256 样本）天然无缝，per-feature seed `${seed}_c${i}` 让海岸线跨重绘稳定；位移幅度 ∝√edgeLen 是布朗尺度不变；calm 区『完全不细分』留给 B-spline，rough 区用向心 Catmull-Rom——一条 path 内混两种曲线算法并显式处理接缝。
- erosion-bake 的 0.5 等值线技巧：二值陆地掩膜做高斯模糊后其 0.5 等值线仍精确落在原矢量海岸线上，shader 用 smoothstep(0.5,0.7,mask) 一步完成水面压平+海岸过渡，零高程线严格贴矢量而非 cell 中心。
- erosion shader 的『未归一化方向向量』：余弦核相位 dot(d,dir) 不归一化 dir → 条纹频率随坡度自动缩放，平地退化为常数=内建淡出+峰顶收尖，无需显式坡度门；runevision 符号技巧 sign(derivative) 反馈让细尺度 gully 锁定走向。
- enforceDownhillCourses：GPU 噪声无法保证河床单调，CPU 后处理沿真实蜿蜒线维护 running-min 河床并以『核心半径+随切深加宽的羽化』圆盘压印，保证渲染网格河流永远下坡——矢量河与放大地形的对齐方案。
- updateHeightmap 的提交期过滤：land/water 限制不是逐点阻止而是拖拽结束后与历史栈顶 diff、把越界（跨 20 阈值）的格整体回滚——实现简单且天然兼容撤销。
- 湖泊 lava/sinkhole 分类用 firstCell%10===0 当 10% 伪随机门——不占 RNG 序列、跨保存/加载稳定的确定性抽签。
- 画廊预览是真跑生成器：14 个模板各在克隆图上执行一遍、seed 存在卡片 data 属性上，『New Map』直接用预览的 seed——所见即所得的预设选择。
- relief 批量放置的 painter 序插入：维护各图标底边 y 的数组，新图标按 z=y+2h 找 nth 后 insert(:nth-child)——无需全量重排的 2.5D 遮挡。
- Voronoi 顶点坐标 Math.floor 取整、pack 顶点恒 3 邻 cell——大量下游算法（isoband 行走、特征周界）依赖这个 3 度性质。
- draw-heightmap 用『每高度一条合并 path』渲染全图（≤202 个 DOM 元素），配 skip/relax/terracing 假 3D 阴影，是 SVG 大地图的性能关键。
- erosion-bake 的 makeKey 用 FNV-1a 直接哈希 grid.h/pack.r/pack.fl 全 typed array 做内容寻址缓存——与 WorldEngine 的缓存哲学不谋而合。
- 所有列出的文件都存在并已通读；heightmap-editor.ts 2100 行分两次读完。

## climate-hydro（气候与水文）

### 编辑 UX 模式

这组编辑器完整展示了 FMG『生成默认 + 手工覆盖』的交互语法，五个模式值得系统性移植：(1) 暂存-提交模式——biome 画刷把改动画进临时 SVG 组（data-* 属性即 diff），Apply 遍历提交 / Cancel 删组，破坏性为零；(2) 表格总览模式——biomes/rivers 都是 sortable header + 搜索过滤 + 页脚聚合 + 行内编辑 + 行↔地图 hover 联动 + CSV 导出 + zoom-to，这是实体浏览器的完备件清单；(3) 派生/旋钮字段分离——discharge/length/width/depth 一律 disabled 只读，widthFactor/sourceWidth/habitability 等作者乘子可写并实时联动重算（habitability→人口、widthFactor→河宽），与 WorldEngine gen/user 双层一一对应；(4) 双档创建——精确通道（逐 cell 点选、逐 cell 改 flux）与自动通道（点源头物理追踪、智能并入既有河网）并存，自动通道保证水文合法性；(5) 环境礼仪——编辑器打开强制开启依赖图层并记录原状态、关闭恢复，破坏性操作确认弹窗并明示级联范围（『支流将被一并删除』），错误引导到正确工具（『改水请去高度图编辑器』），Shift 连续放置，pressed 态切换放置模式。对我们最有价值的是 (1)+(3)：staging buffer + 字段分层直接定义了 React 工作台上 worldstore 编辑的交互协议；而 FMG 没有的撤销栈、锁定/有效期语义我们已更强（worldstore 锁 + 纪元），属于 already_better 部分。

### 渲染架构笔记

FMG 本组渲染架构是『等值线化 + 分层 SVG』：核心基建是顶点链行进（connectVertices / getIsolines）——从任意布尔/阈值分类函数提取 cell 区域的边界顶点环，被等温线、冰川、海洋分层三处复用；链条按深度/用途抽稀（等温线每 4 顶点、海洋层 relax=1+t×−2），再喂 d3 curveBasisClosed 得到有机曲线。着色策略各异：等温线用固定色标 interpolateSpectral（色标锚定 UI 控件 min/max 而非数据范围，保证跨图可比）+ 底色全图矩形；海洋深度用单色 #ecf2f9 半透明堆叠（opacity=0.4/层数）以层叠模拟渐变；河流是闭合变宽多边形（左右偏移点列 + curveCatmullRom alpha=0.1 拼接单 path fill）而非描边线。性能技巧：draw-ice 拼 HTML 字符串一次注入替代 d3 join，增量重绘按 data-id 定位；风向用 Unicode 箭头字符当图元。所有图层是命名 <g>（可样式编辑、可分组），编辑器普遍用 #debug 组画临时控制件（controlPoints/controlCells/vertices）、用 #temp 组做暂存 diff。对 WorldEngine：等值线提取要在球面 cell graph 上重写（rhombus-atlas 跨片行进），变宽多边形转三角带；『固定色标』『暂存组』『增量按 id 重绘』三个原则与渲染技术无关，直接继承。

### 功能明细

#### 纬度带温度模型 calculateTemperatures (public/main.js:946)

**算法**：分段线性纬度模型：热带区 [16°N, -20°S]（注意不对称），带内 tempSea = temperatureEquator − |lat|×0.15；带外线性插值到极地：northernGradient = (tempNorthTropic − tempNorthPole)/(90−16)，southernGradient 同理用 (90+(-20))。默认参数 equator=27°C、北极=-30°C、南极=-15°C（刻意南北不对称模拟真实地球）。海拔递减率：drop = ((h−18)^heightExponent)/1000 × 6.5 °C/km，h<20（水面）不降温。逐格网行计算（同一行共享纬度），结果 clamp 到 Int8 [-128,127]。

**数据模型**：grid.cells.temp: Int8Array（粗网格 cell 级，非 pack 层）；options.temperatureEquator/NorthPole/SouthPole 三个作者旋钮；高度语义：h∈[0,100]，20 为海平面，米数 = (h−18)^exponent。

**编辑 UX**：三个滑杆旋钮（赤道/北极/南极温度）+ heightExponent，改动即全场重算，作者可锁定（locked() 机制）

**渲染**：见 draw-temperature 条目

**取舍**：**already_better** — WorldEngine 已有物理温度场（气候模块），球面上纬度天然连续。但值得抄的是『三旋钮参数化』（赤道温/北极温/南极温各自独立）作为作者覆盖层：小说作者想要一个『南半球更暖』的世界时，这是最直观的控制面。建议在 DAG 气候节点上加 equator/np/sp 三个标量参数做基线场偏置，物理模拟在其上叠加。

#### 行进风带降水模型 generatePrecipitation (public/main.js:995)

**算法**：6 个 30° 风带（tier = |lat−89|/30），默认风向角 [225,45,225,315,135,315]（西南/东北/西南/西北/东南/西北，模拟真实行星风带）。方向分类：isWest 角∈(40,140)，isEast∈(220,320)，isNorth∈(100,260)，isSouth>280||<80（一个角可同时算两个分量）。降水纬度修正表（5°一档）：[4,2,2,2,1,1,2,2,2,2,3,3,2,2,1,1,1,0.5] —— ITCZ x4、副热带高压 x1、50-60° 锋面带 x3、极地 x0.5。modifier=(cells/10000)^0.25×precSlider/100。东西风每行 maxPrec=min(120×modifier×latMod,255)，南北风共享 60×modifier 按 northerly/southerly 计数比例分配。passWind：初始 humidity = maxPrec − h[起点]（起点太高则整行干燥）；逐格直线推进（next=±1 或 ±cellsX）：temp<−5 冻土跳过（不输送水汽）；水格：若下格是陆地 → 海岸降水 += max(humidity/rand(10,20),1)，否则 humidity=min(humidity+5×modifier,maxPrec) 且水面自身 prec+=5×modifier；陆格：下格 h>85 完全不可越 → 倾泻全部 humidity（雨影绝壁），否则 precip = clamp(normalLoss + Δh×(h_next/70)², 1, humidity)，normalLoss=max(humidity/(10×modifier),1)，蒸发回补：precip>1.5 时 +1；humidity=clamp(humidity−precip+evap,0,maxPrec)。

**数据模型**：grid.cells.prec: Uint8Array（粗网格）；风向箭头（⇉⇇⇊⇈ Unicode 字符）作为 SVG text 画进 prec 图层供作者查看。

**编辑 UX**：options.winds 每带角度可调（风向 UI 转盘），precInput 全局降水滑杆

**取舍**：**already_better** — 我们的湿度平流在 cell graph 上做，无方形网格行列假设，且球面无边界。但三点值得吸收：(1) 那张 18 档纬度降水修正表是极好的验证锚——用它对我们模拟输出的纬向平均降水做 sanity check；(2) 『冻土带不输送水汽』(temp<−5 skip) 是我们平流里可加的一行物理近似；(3) (h_next/70)² 迎风坡放大 + >85 全倾泻的雨影双段规则，可做我们放大细节层的快速路径（低频模拟+高频启发式雨影）。

#### 生物群系矩阵 Biomes.getId (src/generators/biomes.ts)

**算法**：5×26 查表矩阵：moistureBand = min(⌊moisture/5⌋,4)，temperatureBand = clamp(20−temp,0,25)。硬覆盖规则优先：h<20→Marine(0)；temp<−5→Glacier(11)；temp≥25 且无河且 moisture<8→Hot desert(1)；湿地规则 isWetland：temp>−2 且 (moisture>40 且 h<25 近海岸) 或 (moisture>24 且 24<h<60 内陆)→Wetland(12)。moisture 计算：prec[gridRef] + 河流加成 max(flux/10,2)，再与陆地邻居的 prec 取平均后 +4 取整（廉价空间平滑）。13 个默认群系带 habitability [0,4,10,22,30,50,100,80,90,12,4,0,12]、movement cost [10,200,150,60,50,70,70,80,90,200,1000,5000,150]（Tundra 1000、Glacier 5000）、icon 加权集（如热带雨林 {acacia:5,palm:3,deciduous:1,swamp:1}）。

**数据模型**：pack.cells.biome: Uint8Array；biomesData 为平行数组结构 {i,name,color,biomesMatrix,habitability,iconsDensity,icons(展开后的加权字符串数组),cost}，可加自定义 biome（id 13-254）。

**取舍**：**adapt** — 我们有 Köppen，分类学上更强。要抄的是『每 biome 的 habitability% 和 movement cost 表』——civ.py 的聚落适宜度和 geoquery travel-time 正需要这张表。建议给 Köppen 类别建同构的 habitability/cost/icon-density 映射表，作为 worldstore 可编辑实体（作者可改某群系宜居度并触发人口重算，FMG 的 recalculatePopulation 联动就是这个语义）。河流 moisture 加成 max(flux/10,2) 也值得加进我们的生物群系输入。

#### 河流水系生成 drainWater/flowDown (src/generators/river-generator.ts:60)

**算法**：陆地 cell 按高度降序遍历（Lagrangian 排水）：fl[i] += prec/cellsNumberModifier，cellsNumberModifier=(cells/10000)^0.25。MIN_FLUX_TO_FORM_RIVER=30：低于阈值只传 flux 不成河。下坡目标：湖泊出口 cell 排除源湖邻居后取最低；海岸 cell 用预存 haven；否则最低邻居。flowDown 汇流仲裁：下游已有河时比较 fromFlux 与 toFlux(=fl−conf)，大者夺取下游河道 id，败者记入 riverParents 成为支流，conf[toCell] 累加被并入的 flux。入湖：lake.flux 累加、inlets 记录、enteringFlux 最大者成为『穿湖主河』。湖泊出口：flux>evaporation 的湖在最低岸 cell 溢出，出口 flux = lake.flux − evaporation；链状湖泊通过检查邻 cell 河 id 保持同一河流身份（chain lakes retain identity）。边界 cell 以哨兵 -1 记录出图。defineRivers：<3 cell 的河丢弃；mouth = cells[len−2]（倒数第二格，最后一格是承接水体）；conf 重算：汇流点 conf = 除最大入流外其余入流之和。

**数据模型**：pack.cells.fl:Uint16（m³/s 语义）、r:Uint16（河 id）、conf（汇流量）；pack.rivers: {i,source,mouth,parent,basin,length,discharge,width,widthFactor,sourceWidth,name,type,cells[],points?}；riverParents 字典构建支流树。

**取舍**：**already_better** — 我们有多重网格侵蚀物理 + stream-burning + 支流树，flux 来自真实降水场。但两个语义值得核对移植：(1) 『湖泊水量平衡决定出口』（flux>evaporation 才有 outlet）——我们的矢量河流提取是否显式处理湖泊蒸发截流？endorheic 盆地对小说地理（咸海式设定）很重要；(2) 汇流仲裁『大 flux 夺 id、小者为支流』+ conf 字段（记录汇入量）是干净的支流树构建法，可对照我们支流树的 id 稳定性。

#### 洼地填充 resolveDepressions + 高度扰动 alterHeights (river-generator.ts:296)

**算法**：alterHeights：h + t/100 + mean(邻居 t)/10000，t 是离岸距离场——确定性微扰破平地平局，使排水方向稳定可重放（不用 RNG）。resolveDepressions：land 按高度升序迭代抬升 h[i]=minNeighbor+0.1；湖泊作为整体参与：l.height=minShore+0.2；迭代预算分段——>75% 预算后强制封闭问题湖（shoreline 高度还原、lake.height=minShore−1、标记 closed），>85% 后不再处理湖；失败保护：滑动窗口 5 次迭代 progress（depressions 增量）之和 >0 说明振荡恶化，回滚 alterHeights 重来并放弃。maxIterations 由 UI 滑杆控制。

**取舍**：**already_better** — 我们的侵蚀物理天然产生可排水地形（且应使用 priority-flood 级算法）。值得抄的是『确定性 tie-break 扰动』思想：在球面 IGM 上做河流提取时用离岸距离场（或任何已有确定性场）做 ε 扰动，保证同一输入永远同一河网——对内容寻址缓存命中至关重要。振荡检测回滚也是稳健工程细节。

#### 河宽/流量启发式 getOffset/getWidth (river-generator.ts:400)

**算法**：全部常数：FLUX_FACTOR=500，MAX_FLUX_WIDTH=1，LENGTH_FACTOR=200。offset = widthFactor×(lengthWidth+fluxWidth)+sourceWidth；fluxWidth = min(flux^0.7/500, 1)；lengthWidth = pointIndex/200 + Fib[pointIndex]/200（斐波那契数列 [1,1,2,3,5,8,13,21,34]/200 做前段加速增宽）；sourceWidth = min(flux^0.9/500, 1)。最终河口宽 km = (offset/1.5)^1.8，代码注释附真实校准表（Amazon 6000m、Volga 6000m、Mississippi 1300m、Nile 450m、Muchavets 40m）。widthFactor：主干 = 1.2×默认，默认 = rn(1/(cells/10000)^0.25, 2)（分辨率无关化）。discharge 直接取 mouth cell 的 fl 值当 m³/s。downcutRivers：h≥35 的河道 cell 下切 min(⌊fl/邻居均flux⌋,5)。

**取舍**：**already_better** — 我们用 Leopold 水力几何 w=7√Q，物理上更对（FMG 的宽度一半是『视觉长度增宽』而非水力学）。差距在 FMG 反而混入了渲染需求（线宽随下游变粗的视觉节奏）；我们在球面渲染河流时可保留纯 Leopold，但 UI 层保留 widthFactor/sourceWidth 两个作者乘子（见河流编辑器条目）作为 user-layer 覆盖字段进 worldstore。

#### 蜿蜒插值 meander + 锐角松弛 (src/utils/pathUtils.ts:370)

**算法**：纯函数（PRD 驱动重构后无全局依赖）。锚点=cell 中心（可覆盖），-1 哨兵投影到最近图边。每对锚点插 0-2 个垂线偏移点：振幅 meanderVal = 0.5 + 1/step + max(0.5 − step/100, 0)，step 从 startStep 起每 cell +1（源头大摆、下游收敛）；startStep：源头在水上（湖出口）=1，陆上=10；水域 cell 振幅 ×0.25 (WATER_MEANDER_SCALE)。两点插值（step<20 且 dist²>64）：在 1/3、2/3 处，垂偏一正一负（第二点半幅），制造 S 弯；单点插值（dist²>25 或 <6 cells）：中点垂偏。dist²≤25 且 ≥6 cells 跳过。relaxAcuteAngles：≤4 轮，把非锚点关于其前后锚点连线做镜像反射，若三个相邻角的 Σmax(cosθ,0) 下降则采纳（锚点永不动）——廉价消锐角。

**取舍**：**adapt** — 我们有 λ≈11w 的物理蜿蜒细化，几何质量更高。要抄的是两个工程点：(1) relaxAcuteAngles 的『反射消锐角、锚点不动』后处理——我们蜿蜒细化后的折线在 cell 边界处也会出锐角，此技可直接用于球面切平面局部坐标；(2) meander 的 {points, anchorIndices} 返回形态——锚点索引元数据让下游消费者（路线、动画）能对齐 cell↔几何，这正是我们 travel-time 路线要沿河渲染时需要的 API 形状。

#### 河流多边形渲染 getRiverPath (river-generator.ts:425)

**算法**：不是描边线而是闭合变宽多边形：对每个蜿蜒点取前后点连线方向角 angle=atan2(y0−y2,x0−x2)，左右各偏移 offset（getOffset，flux 取运行最大值保证宽度单调不减），得左右两条点列；d3 curveCatmullRom(alpha=0.1) 分别生成路径，右列 reverse 后与左列拼接（左路径去掉 M 段取 'C' 起始）成单一闭合 fill path。每点携带 [x,y,flux] 三元组。

**渲染**：单 <path> fill 渲染一条河，无 stroke 宽度伸缩问题，天然支持源头尖、河口宽

**取舍**：**adapt** — 我们 2D 导出（equirect 已降级为导出用途）与未来 MapLibre 城市图都需要河流几何。变宽多边形 + Catmull-Rom 是正确输出形态：从我们的矢量河流（已有 Leopold 宽度逐点值）生成左右偏移闭合环，导出 MVT/GeoJSON。球面 3D 侧则转为三角带（沿河中心线两侧偏移在切平面内做）。直接可落地到『矢量叠加层缺失』这个 gap。

#### 河流命名与类型 specify/getType (river-generator.ts:458)

**算法**：名字取 mouth cell 所属文化的 Names.getCulture。类型加权随机：小河阈值 = 全部河流长度第 15 百分位（threshold=ceil(n×0.15)）；isFork = (id%3==0) 且有 parent——用 id 取模做确定性 1/3 抽样；权重表 main.big={River:1}, main.small={Creek:9,River:3,Brook:3,Stream:1}, fork.big={Fork:1}, fork.small={Branch:1}。basin 递归沿 parent 上溯。

**取舍**：**adopt** — 直接可抄给 civ.py：河名按河口文化区取词根 + 支流按尺寸档位取通名（Creek/Brook/Fork）——这对小说一致性检查是刚需（同一水系的支流命名应有从属感）。『id 取模做确定性抽样』比 RNG 好，重生成稳定。落到 worldstore river 实体的 gen 层字段。

#### 湖泊气候数据与水量平衡 Lakes.defineClimateData/detectCloseLakes (src/generators/lakes.ts)

**算法**：lake.height = min(岸线高度) − 0.1。flux = Σ岸线 cell 的 prec。temp = 岸线均温（<6 cell 的小湖取 firstCell 温度）。蒸发 Penman 近似：单 cell evaporation = ((700×(temp + 0.006×heightMeters))/50 + 75)/(80 − temp)，×湖 cell 数；heightMeters=(h−18)^exponent。出口 = 最低岸 cell（closed 湖除外）。detectCloseLakes：从最低岸 cell BFS，上界 lake.height+ELEVATION_LIMIT（UI 滑杆）：触到 ocean 或更低的湖 → 可开放；否则 closed（深洼地内流湖）。editor 语义：supply>evaporation 且有 outlet = 淡水；evaporation>supply = 咸水；差距大 = 干湖。cleanupLakeData 在河流生成后删临时字段并校验 inlets/outlet 引用还活着。

**数据模型**：feature(lake): {height,flux,temp,evaporation,inlets[],outlet,closed,shoreline[],cells,firstCell,group(freshwater/salt/sinkhole/frozen/lava/dry)}

**取舍**：**adopt** — 这是本组最值得整体搬运的启发式：湖泊『补给 vs 蒸发 → 淡/咸/干 + 内流/外流』分类，成本极低（岸线求和 + 一个公式），叙事价值极高（盐湖、干盐滩、里海式封闭水系都是小说地理素材）。落到我们矢量河流管线的湖泊节点：用真实降水场做 flux、真实温度场进 Penman 近似，产出 worldstore lake 实体的 salinity/regime 字段，供 geoquery describe 使用。

#### 深洼地补湖与近海湖开口 addLakesInDeepDepressions/openNearSeaLakes (main.js:766/827)

**算法**：补湖：对每个陆地局部极小 cell，BFS 上界 h+elevationLimit 探测能否触到水（h<20）；不能 → 该 cell 及同高邻居设 h=19 成湖。开口：LIMIT=22——湖与海洋之间若存在 h≤22 的海岸 cell 则凿穿（h=19），整湖并入海洋 feature（注释引用真实案例 Ancylus Lake：近海湖入流大总会溢穿门槛）。Atoll 模板跳过。

**取舍**：**adapt** — 两个后处理对我们侵蚀输出同样有用：侵蚀模拟在低分辨率频段可能留下伪洼地，『补湖 or 开口』是作者可选的二元策略。建议做成 DAG 节点参数（lake_threshold / breach_limit），在河流提取前跑，且 breach 行为要记 provenance（作者可能想保留内海）。

#### 冰川/冰山生成 Ice.generate (src/generators/ice-generator.ts)

**算法**：冰川：陆地且 temp≤−8 的 cell 集合跑 getIsolines（顶点链行进提取连通区轮廓多边形），clip 到视口，每连通区一个 polygon。冰山：水面 temp≤0、非湖、P(0.8) 跳过（只保留 20% 候选）；size = clamp((1−normalize(t,minTemp,1))×0.8 × rand(0.8~1.2), 0.1, 1)，海岸线 cell ÷1.3；形状 = 该 cell 的 Voronoi 多边形各顶点向质心 lerp(size) 收缩。id 分配填空隙复用（getNextId 扫 gap）。

**数据模型**：pack.ice: (Glacier{i,points[],offset?} | Iceberg{i,points[],cellId,size,offset?})[]——注意存的是最终几何而非规则，编辑结果天然可持久化。

**渲染**：见 draw-ice 条目

**取舍**：**adapt** — 我们有温度场但没有冰实体。两档阈值（陆冰 −8、海冰 0）+ 温度驱动的冰山尺寸直接可移植到球面：冰盖作为 CellField mask（喂 Köppen EF 一致性），冰山作为稀疏点实体进 worldstore（航线危险区——geoquery news/travel 的调味料）。『存几何不存规则』的实体化思路与我们 gen/user 双层正合：gen 层生成，user 层作者拖拽/增删。

#### 等温线渲染 drawTemperature (src/renderers/draw-temperature.ts)

**算法**：step = max(round((maxT−minT)/5),1)（约 5 条等温线）。对每条 isoline t：找 temp==t 且未访问的 cell，findStart 定起始顶点（邻居 temp<t 处），connectVertices 顶点链行进（ofSameType: temp>=t）；链抽稀保留每第 4 顶点 + 图边顶点；<6 点丢弃。渲染：先铺整图底色矩形（minTemp 色），再按 t 递增叠 fill 多边形，色 = interpolateSpectral(1−(t−tMin)/delta)（tMin/tMax 取自 UI 控件的 min/max 属性做固定色标！），描边 darker(0.2)，curveBasisClosed。标签：每条链顶部中心 + （>20 点的链）底部中心，用 leastIndex 最小化 y ± |x−xCenter|/2 的复合准则，两标签距² ≤100 去重，距边 20px 内丢弃；按 °C/°F 单位换算。

**取舍**：**adapt** — 球面主视图我们用顶点色，不需要它。但导出的 2D 图集（equirect 导出路径）加『带标注等温线』是廉价高级感：色标固定到全局温度范围（而非当前图 min/max）这个细节保证多图可比。等值线提取我们应在 cell graph 上做 marching（跨 rhombus-atlas 边界处理），标签放置的『top-center/bottom-center + 复合准则』可照抄。

#### 海洋深度分层渲染 OceanLayers (src/renderers/ocean-layers.ts)

**算法**：用 cells.t（离岸距离，水为负 −1..−9+）画堆叠轮廓：limits 可为用户 CSV 或 'random'——随机模式从 −9 到 −1 以 odd=0.2 起、不选则翻倍的概率选层（越浅越必选）。每层顶点链行进（connectVertices 变体，比较 t==t−1 边界），链抽稀 relax=1+t×(−2)（越深抽稀越狠），clipPoly 到视口，curveBasisClosed，统一 fill #ecf2f9、每层 opacity=0.4/层数 —— 层叠加自然形成越深越白/越蓝的渐变。

**取舍**：**skip** — 纯 2D 美术技巧，靠固定色半透明堆叠模拟深度渐变。我们球面渲染直接有逐 cell 测深顶点色，等深线导出如需要可复用等温线的同一套 marching 基建。唯一记一笔：『离岸距离场当免费等深线』的偷懒思路——我们如果想要风格化（非写实）海洋，distance-to-coast 分层比真实测深更好看。

#### 冰层渲染与增量重绘 draw-ice (src/renderers/draw-ice.ts)

**算法**：全量绘制：把所有 ice 拼成一个 HTML 字符串一次性 .html() 注入（避免逐元素 d3 join 开销）。增量：redrawIceberg/redrawGlacier(id) 按 data-id 属性选择器找 polygon，不存在则 insertAdjacentHTML 追加，更新 points 与 offset transform。拖拽偏移存 ice.offset 走 transform，几何 points 不动——位置编辑与形状数据解耦。

**取舍**：**skip** — SVG 字符串拼接是 D3 性能补丁，Three.js/React 下无意义。『offset 与几何解耦』的编辑语义（拖动只改 transform 不改点集）倒是通用教训：我们 worldstore 实体的 user 层覆盖应存『变换/增量』而不是覆写 gen 几何，便于重生成后重放。

#### 生物群系编辑器 biomes-editor (src/controllers/biomes-editor.ts)

**算法**：笔刷指派：进入 customization=6 模式后，选中表格行为当前 biome，拖拽画刷半径 1-100（r>5 用 findAll 半径查询否则单 cell），只允许陆地；关键设计——改动不直接写 pack.cells.biome，而是往 #temp SVG 组里塞带 data-cell/data-biome 属性的 polygon 作为『暂存 diff 层』，Apply 时遍历 temp 组提交、Cancel 直接删组。水面 cell painting 被拒并提示去改高度图。restoreInitialBiomes 一键回到 getDefault()+define() 重算。

**编辑 UX**：表格总览：可排序列（名称/宜居度/cells/面积/人口）、百分比↔绝对值切换、CSV 导出、图例开关、行 hover ↔ 地图高亮（stroke 过渡动画 2s）；行内改名、改色（色板弹窗）、改 habitability 即触发 recalculatePopulation 联动；自定义 biome 追加（上限 255，只有 0 cell 时可删，删除是 name='removed' 墓碑而非收缩数组）

**取舍**：**adopt** — 本组最值得整套抄的编辑 UX：(1) 『暂存层 + Apply/Cancel』正是我们 gen/user 双层的交互化——React 侧画刷改动先进 staging buffer，提交才写 user 层并打脏 DAG 下游；(2) 表格 overview + 行↔球面高亮联动是所有 worldstore 实体列表该有的模式；(3) habitability 可编辑并联动人口重算 = 参数级作者控制的范本。球面画刷用测地距离取 cell 集（我们已有 cell graph BFS）。

#### 河流编辑器 river-editor (src/controllers/river-editor.ts)

**算法**：控制点直接操纵：river.points 覆盖锚点（默认 cell 中心），拖点实时 redraw（meander→getRiverPath→路径替换），长度实时 = SVG getTotalLength()/2（除以 2 因为是闭合多边形周长！）；点拖进新 cell 时松手才提交拓扑：r[旧cell]=0、r[新cell]=riverId 且交换两 cell 的 fl 值（数据跟随几何）。点击河体在最近线段处插入控制点（getSegmentId），点击控制点删除。widthFactor(0.1-4)/sourceWidth(0-3) 数字输入实时重算宽度。mainstem 下拉改 parent 并级联更新 basin。删除确认后级联删除全部支流（Rivers.remove 按 parent/basin 过滤）。elevation profile 按控制点所在 cell 序列出高程剖面图。

**编辑 UX**：打开时强制开 rivers+cells 图层（记住原状态、关闭编辑器时恢复）；名称三通道：文化生成/随机词根/手输；派生字段（discharge/length/width）disabled 只读，作者旋钮（widthFactor/sourceWidth）可写——生成值与覆盖值分离清晰

**取舍**：**adapt** — 我们的矢量河流编辑正缺这套：控制点拖拽 → 局部重跑蜿蜒细化 → 球面上实时预览。要点移植：(a) points 覆盖字段进 worldstore user 层（gen 重算时锚点保留、蜿蜒重放）；(b) 『几何动则数据跟着搬』的 cell 交换语义改为我们的 stream-burning 局部重跑；(c) 只读派生 + 可写乘子的字段分类直接映射 gen/user 层；(d) 高程剖面工具对小说作者（行军路线累计爬升）非常有用，geoquery route 已有数据。

#### 河流创建双通道 river-creator + addRiverOnClick (river-creator.ts / tools.js:737)

**算法**：手动通道：逐 cell 点选组路径（再点取消），每 cell 的 flux 可在列表里改（直接写 cells.fl 影响宽度），≥2 cell 才可完成；parent 自动 = 末 cell 已有河的 id。自动通道 addRiverOnClick：点一个 cell → resolveDepressions 后沿最陡下降自动追踪到底；终止三态：入水（若是湖则登记 inlet，parent=湖的 outlet——自动接入下游水系）、出图（-1 哨兵）、撞上已有河——此时比较长度：新河较短则做支流（conf+=flux）；新河较长则『夺取』旧河 id：旧河上游段释放（r=0、flux 重置为 prec）、下游段并入，新河成为该河新源头。Shift 连续放置。

**编辑 UX**：两档创建：精确控制（逐 cell）vs 一键自然（物理追踪），后者保证水文合法性

**取舍**：**adopt** — 『点一下源头、物理自动追踪、与既有河网智能合并』是我们该做的默认河流创建交互——我们有真实流向场，追踪更可靠。夺取/支流的长度仲裁规则值得原样保留（它保证了『真正的干流』语义，即作者点出的新源头如果更远，河名应从那里算起——恰好符合真实河流正源之争的逻辑，小说素材+1）。

#### 河流总览表 rivers-overview (src/controllers/rivers-overview.ts)

**算法**：全河流表：名称/类型/流量/长度/河口宽/流域，六列可排序，搜索框同时过滤 name/type/basin；页脚聚合（数量 x of y、平均流量/长度/宽度）；basin 高亮模式：按 basin 分组用 d3 category10 循环着色所有河流 fill；CSV 导出从 DOM data-attributes 读（导出所见）；行 hover 红色描边、zoom-to 定位、行内编辑/删除、全删。

**编辑 UX**：overview 模式标准件：sortable header + search + footer 聚合 + 行内操作 + 批量删除确认

**取舍**：**adopt** — worldstore 实体浏览器的直接模板。basin 着色开关尤其好——一键看清流域结构，我们支流树数据现成。React Flow 工作台旁边放实体表格 + 球面联动高亮，列结构照抄（discharge/length/width 均已有且物理更准）。

#### 湖泊编辑器 lakes-editor (src/controllers/lakes-editor.ts)

**算法**：只读水文面板：面积、岸线长（polygonLength）、海拔、平均/最大深度（湖内 cell 高度均值/最小值换算）、supply、evaporation、inlets（数量+悬停显名单）、outlet 名——tooltip 里明示淡/咸/干判定规则教育作者。组管理：湖 group = SVG <g> 归属（freshwater/salt/sinkhole/frozen/lava/dry 六个内置组不可删 + 自定义组增删，删组时成员回落 freshwater）。岸线顶点拖拽微调：直接改 pack.vertices.p，实时更新湖 path 与面积，drag end 时重绘 states/provinces/borders/biomes/religions/cultures 全部依赖层；tooltip 明确警告『仅供微调，实质高度请改高度图』。

**编辑 UX**：『显示派生量+解释规则』的面板哲学；分组即样式（group=渲染组）；破坏性编辑给出正确工具指引

**取舍**：**adapt** — 顶点拖拽不适合我们（cell-native，几何派生自网格），对应操作应是『局部改高度场 → 重跑该湖』。但要抄：(1) 水文面板的字段清单+判定规则展示（supply/evaporation/regime 教育性 tooltip）——描述型数据正是喂 geoquery describe 的内容；(2) 湖类型六分类（freshwater/salt/sinkhole/frozen/lava/dry）做 worldstore 枚举，其中 frozen/lava/dry 是纯叙事型分类，气候数据可自动建议、作者可改。

#### 冰编辑器 ice-editor (src/controllers/ice-editor.ts)

**算法**：选中冰川/冰山进入拖拽模式（全部 ice 元素临时 draggable，位移存 offset）；冰山专属：randomize（借随机别的 cell 的多边形做新形状）、尺寸滑杆 0.05-2（点集绕 cell 中心按 newSize/oldSize 重缩放）；添加模式：按钮 pressed 态 + crosshair 光标 + 点图放置，Shift 连放；删除带确认。

**编辑 UX**：pressed-toggle + shift-repeat 的放置模式；类型分派 UI（冰川隐藏冰山专属控件）

**取舍**：**skip** — 冰实体本身按 ice-generator 条目 adapt；这个编辑器交互没有超出通用『点选实体、点击放置、shift 连放』模式的内容，我们实体编辑框架实现一次即覆盖。不单独立项。

#### PRD：可航河流航线 navigable-river-routes (docs/prd)

**算法**：把海路寻路扩展到可航河道：MIN_NAVIGABLE_FLUX=100 硬阈值；burg.port 语义从『毗邻水体』拓宽为『最终可达水体』——resolveDrainFeature(cell)/resolveLakeDrainFeature(lakeId) 沿 outlet 链穿开放湖直到海洋/封闭湖/出图（带 visited 环保护，已实现于 river-generator.ts:534）；riverAdjacency 由 pack.rivers[*].cells 逐对构建，寻路成本函数只放行 adjacency 内的河道跳步——防止 Voronoi 相邻但分属不同流域的 cell 互穿（分水岭不变量，有单测锁定）；汇流点因出现在多条河的 cells 里自然成为换乘点；封闭湖（里海式）形成孤立港口网络。上游成本不对称显式列为 out of scope。

**取舍**：**adapt** — 与我们 geoquery route/travel-time/news_arrival 完美对口：(1) 『最终泄水体』分组是水运可达性的正确抽象，我们支流树+湖泊 outlet 数据现成，可直接实现 drain-feature 解析给聚落打 port 属性；(2) 分水岭不变量（沿河道邻接表而非空间邻接寻路）必须抄——我们 cell graph 上做水路寻路会犯一模一样的错；(3) news 传播延迟应给水路与陆路不同速率，河港城市信息辐射范围立刻不一样，这是小说素材生成器。上游/下游成本不对称我们应该做（他们没做）：顺流 vs 逆流速度差影响 travel-time 方向性。

#### PRD：航线沿河蜿蜒渲染 meandering-river-routes (docs/prd)

**算法**：问题：航线只知 cell 中心，直弦切弯、画到河体外。方案：meander() 提纯为无全局纯函数（已落地 pathUtils）；riverAdjacency 升级为方向感知 riverEdges: Map<cellA, Map<cellB,{riverId,fromIndex}>>；路线点生成时把 cell 序列切成『同河且 index 严格单调』的最大 run，每 run 以河的 source→mouth 规范序调 meander，startStep = 10 + 该 run 首 cell 在河中的 index —— 相位匹配使航线振幅与河多边形在同一 cell 处完全一致；逆流 run 输出 reverse（直接反序输入会导致垂偏方向镜像到对岸）；汇流处 run 断开、边界锚点各贡献一次；内部插值点第三坐标带前一锚点的 cellId，靠下游 dedupe 守卫零改动兼容。

**取舍**：**adopt** — 两条工程原则直接吸收：(1) 『相位匹配』——凡是两层几何要叠合（我们的贸易路线叠河流、道路叠地形等高线），必须共享同一参数化函数+同一相位种子，而不是各自近似；(2) 规范序+按需反转解决方向依赖的垂线偏移——我们球面蜿蜒同样有手性问题。落点：M15 城市图之前，先给 geoquery route 的可视化（球面矢量层）做河段沿河渲染，复用矢量河流的蜿蜒中心线即可（我们比 FMG 有利：蜿蜒几何是持久化的，不需重算相位）。

### 值得偷的技巧

- 降温的南北极默认温度刻意不对称（北极 -30 / 南极 -15），单参数即模拟真实地球海陆半球差——作者旋钮设计比『一个极温』高明
- alterHeights 用离岸距离场做确定性 ε 扰动（h + t/100 + mean(邻t)/10000）破平地流向平局——零 RNG、完全可重放，对我们内容寻址缓存是关键性质
- resolveDepressions 的三段迭代预算（75% 后强制封湖、85% 后弃湖、滑窗5次进度恶化即回滚）——把不收敛处理成显式产品语义（closed lake）而非失败
- 湖泊蒸发用 Penman 公式变形 ((700(T+0.006h))/50+75)/(80−T)，配合 flux 求和给出淡/咸/干湖三态——一行公式换一个叙事维度
- meander 振幅 0.5+1/step+max(0.5−step/100,0) 随下游收敛 + 水域×0.25 + 『startStep 相位匹配』让两条独立渲染的曲线像素级叠合（meandering-routes PRD 的核心洞见）
- relaxAcuteAngles：非锚点关于锚点基线反射、局部角代价下降才采纳、≤4 轮——不引入新点、不动锚点的消锐角术
- 河宽用斐波那契数列 [1,1,2,3,5,8,13,21,34]/200 做前 9 段渐宽 + (offset/1.5)^1.8 换算 km，代码注释里附 14 条真实河流河口宽度做校准表
- river.cells 末位约定：最后一格是承接水体、mouth=cells[len−2]、出图用 -1 哨兵——下游一切消费者（drain 解析、航线邻接）都依赖这个约定
- addRiverOnClick 的『夺取』规则：新溯源比旧上游长则接管旧河 id 并释放旧上游——恰好复刻真实世界『正源之争』语义
- 航线寻路的分水岭不变量：用 pack.rivers 的 cells 逐对邻接表限制水路跳步，Voronoi 空间相邻不等于水文连通——我们球面 cell graph 寻路会犯同样的错，必须预防
- 汇流点免特殊处理：cell 同时出现在多条河的 cells 数组里，邻接表自动并集成为换乘点（两份 PRD 都靠这个性质白拿正确性）
- biome 画刷的暂存层用 SVG <g> + data-* 属性存 diff（不碰真数据），Apply 才提交——DOM 即 staging buffer，Cancel 一键删组
- 灌木级细节：冻土 temp<−5 直接中断水汽输送；海岸降水量除以 rand(10,20) 引入海岸随机性；山体 >85 高度全倾泻降水
- 冰山形状随机化 = 借任意别的 cell 的 Voronoi 多边形平移缩放——免费的『自然感』形状库
- grid（粗规则网格，气候）与 pack（细化 Voronoi，水文/文明）双图结构，经 cells.g 映射；气候低频、水文高频——和我们 CellField(freq) 参数化频率思想同构，可互相印证
- 所有列过的文件都存在并已精读；额外补读了 lakes.ts、pathUtils.ts(meander)、tools.js(addRiverOnClick)、main.js(温度/降水/湖泊后处理)，因为它们承载了任务点名的核心算法

## peoples-polities（族群与政体：cultures/religions/states/provinces/zones 生成器 + 6 个编辑器 + hierarchy-tree + namesbase + 州标签/边界渲染器）

### 编辑 UX 模式

FMG 族群政体编辑器是一套高度一致的『表格总览 + 地图直接操作』双面板范式，六个编辑器共享同一心智模型：(1) 实体=表格行，行内联控件（色块/名称/类型/expansionism/namesbase）即改即写数据层，但 growth 相关属性有『auto-apply changes』开关——关闭时攒批改动等 Recalculate 按钮统一重算，开启时任何改动（包括拖动文化中心）即时重跑扩张并重绘，这是『参数可控生成』与『手工精修』的桥梁；(2) 笔刷重指派是独立模式（customization 状态机编号 2/4/7/10/11），改动先落 SVG 暂存层可撤销（innerHTML 快照栈）可放弃，Apply 才提交，且有保护语义（protect 不覆写、首都/省中心 cell 不可刷、锁定实体不可覆写）；(3) 锁定（lock）是第一公民：每行一个锁图标，重生成时锁定实体保 id 保领土，锁还会沿层级传播（锁州→锁其省份）；(4) 破坏性操作（删除/合并/独立/释放全部）一律确认对话框 + 级联清理 + 自愈 pass（adjustProvinces 处理州界改动后的省份分裂）；(5) 辅助视图丰富：百分比模式、气泡图/treemap、谱系树（可拖拽改父）、外交矩阵、CSV 往返。对 WorldEngine 的直接启示：工作台的文明层编辑应复刻『auto-apply 开关 + 显式 Recalculate』『笔刷暂存层 + Apply/Cancel』『锁定实体参与重生成协议』三件套，并把每个破坏性编辑建模为 worldstore 纪元事件以获得可回溯的世界史。

### 渲染架构笔记

SVG 分层架构：每个实体域一个 <g> 图层（#cults 文化、#relig 宗教、#regions 内含 #statesBody+#statesHalo、#provs 内含 #provincesBody+#provinceLabels、#zones、#borders 内含 #stateBorders/#provinceBorders、#labels>#states），实体级元素用 id 约定（#state{i}、#state-gap{i} 缝隙描边、#state-border{i} halo、#province{i}、#zone{i}）。关键技巧：(1) 边界层是每层单一 <path>（所有链 concat），不做实体级拆分，渲染开销极低；(2) 标签用 defs>#textPaths 中的隐藏路径 + <textPath startOffset=50%> 曲线文字，字号用百分比且 tspan 分行；(3) zones 用 SVG pattern 填充 url(#hatch{n})，语义上『覆盖而非占据』；(4) 编辑暂存层 <g id=temp> + 逐 cell polygon(getPackPolygon)，d3 data-join 以 `${zoneId}-${cell}` 复合键管理增删；(5) 高亮动画：取实体 path 的 getTotalLength 后做 stroke-dasharray 插值 attrTween，产生描边游走效果，时长 (len+5000)/2；(6) fog/focus 用 defs 中按 path 剪出的遮罩（fog(id,path)/unfog）；(7) 纹章 COA 惰性渲染：COArenderer.trigger(id, coa) 只在表格行/对话框首次引用 <use href=#stateCOA{i}> 时生成；(8) debug 层承载文化/宗教中心拖拽点与标签调试采样点。对 WorldEngine：层→Three.js 球面 overlay（顶点色/线集/screen-space 标签）或 MapLibre MVT 层的映射关系基本一一对应，暂存层对应前端 override map，dasharray 高亮可用 shader 或屏幕空间线重现。

### 功能明细

#### 文化预设集与选址适应度函数 (culture sets + sort fitness)

**算法**：8 套预设集（european/oriental/english/antique/highFantasy/darkFantasy/random/all-world），每个文化 = {name, base(namesbase id), odd(入选概率), sort(适应度函数), shield}。适应度由 4 个原语组合：n(cell)=ceil(s/sMax*3) 归一化 cell 得分；td(cell,goal)=|temp-goal|+1 温度偏差费；bd(cell,biomes,fee=4) 非亲和生物群系费；sf(cell,fee=4) 非海岸费（haven 非湖）。例：Norse=n/td(i,5)，Elladan=(n/td(18))*h[i]（山地希腊），Inuk=td(i,-1)/bd([10,11])/sf。选取时对 defaultCultures 做拒绝采样：随机抽 index，P(odd) 通过才收，最多 200 次尝试后强收。锁定文化优先保留并重编号。

**数据模型**：Culture{i, name, base, type(7种), center(cellId), color, expansionism, origins[], shield, code(缩写), lock, removed, cells/area/rural/urban 统计}; id 0 恒为 Wildlands。

**编辑 UX**：culturesSet 下拉带 data-max 限制数量；人口不足时自动降数并弹警告。

**取舍**：**adapt** — 落到 civ.py 文化种子选择：把 sort 原语改为我们的场（温度场、Köppen、离海距离、河流 Q），预设集做成 JSON 配方喂 DAG 节点参数。odd 拒绝采样保留——作者可调「这类文化出现概率」。

#### 文化中心放置 placeCenter（间距递减 + 偏置采样）

**算法**：populated cells 按 sort 降序排；候选 = sorted[biased(0, len/2, 5)]（幂偏置偏向头部）；间距 spacing 初始 (graphWidth+graphHeight)/2/count，每失败一次 spacing*=0.9，用 d3.quadtree.find(x,y,spacing) 查冲突，最多 100 次尝试。保证高分点优先且互相隔开，失败时优雅退化（间距自动松弛）。

**取舍**：**adopt** — 极简且鲁棒的 poisson-disc 替代。球面版：quadtree 换成 cell-graph BFS 半径检查或 3D kd-tree（单位向量点积>cosθ），spacing 用弧长。civ.py 直接可用。

#### 文化类型分类 defineCultureType（7 型启发式）

**算法**：按中心 cell 判定：h<70 且 biome∈{1,2,4}(热沙漠/冷沙漠/草原)→Nomadic；h>50→Highland；对岸 feature 是 lake 且 cells>5→Lake；(harbor 且非湖 P(0.1)) 或 (harbor===1 P(0.6)) 或 (isle P(0.4))→Naval；有河且 fl>100→River；t>2(内陆) 且 biome∈{3,7,8,9,10,12}→Hunting；否则 Generic。类型决定后续扩张成本表。

**取舍**：**adopt** — 直接借鉴：用我们的高程/流量/湖泊 feature 字段替换阈值（h 阈值换成米制，fl 换 Q m³/s）。类型标签本身对小说套件很有用（『游牧民族』『高地民族』直接进 worldstore 实体描述）。

#### expansionism 生成（竞争力参数）

**算法**：基数按类型：Naval/Nomadic 1.5，Highland 1.2，Generic 1，River 0.9，Lake 0.8，Hunting 0.7；最终 = rn((random()*sizeVariety/2 + 1) * base, 1)，sizeVariety 为全局滑条。states 版为 rn(random*sizeVariety+1,1)。它是所有 cost-field 的除数，决定势力范围相对大小。

**取舍**：**adopt** — 作为文化/政体实体的一级可编辑参数暴露在工作台表格里（FMG 的『改 expansionism→点 Recalculate』回路就是我们要的作者可控编辑）。

#### 文化扩张 cost-field 生长（Dijkstra 洪泛）

**算法**：FlatQueue 优先队列多源 Dijkstra，源=各文化 center，priority=累计成本。邻 cell 成本 = (biomeCost + biomeChangeCost + heightCost + riverCost + typeCost)/expansionism。biomeCost：与文化母 biome 相同=10；Hunting 非亲和 = biomesData.cost*5；Nomadic 在森林(4<biome<10) = cost*10；其余 = cost*2。biomeChange=跨 biome 加 20。heightCost：Lake 型过湖=10；Naval 过水=area*2；Nomadic 过水=area*50；一般过水=area*6；Highland 在 h<44 = 3000，h<62 = 200，否则 0；一般 h≥67=200，h≥44=30。riverCost：River 型有河=0 无河=100；他型有河=minmax(fl/10,20,100)。typeCost：t=1 海岸线 Naval/Lake=0、Nomadic=60、其他=20；t=2 时 Naval/Nomadic=30；内陆(t≠-1) Naval/Lake=100。maxExpansionCost = cells.length*0.6*neutralRate（越界即停，留出无主地）。只有 pop>0 的 cell 被赋值文化（水面与荒地可穿越但不占据）。锁定文化的 cell 不被覆写。

**数据模型**：cells.culture: Uint16Array；cost[] 稀疏数组做 visited/relax。

**取舍**：**adapt** — civ.py 文化扩张升级到这套完整成本表：把常数迁到球面 cell graph（area 用真实球面面积 km²，h 阈值换米，fl 换 Q）。物理模拟给我们更真的 biome/河流输入，FMG 成本表作为快速启发式路径保留，作者调 expansionism/neutralRate 即时重算。

#### 锁定实体重生成语义（lock & regenerate）

**算法**：重生成时：locked 文化保留对象与其 cells（先按旧 id 收集 cell，再重映射到新 id），未锁 cell 清零后重扩张且不得覆写锁定文化的 cell。宗教更严格：locked 宗教保留原始数组下标，用 {name:'Removed religion', removed:true} 占位垫齐 id，origins 过滤为仅指向其他 locked。省份 lock 及『州 lock 连带省份 lock』双层，regenerateLockedStates 可穿透。

**取舍**：**adapt** — worldstore 已有 gen/user 双层+锁+纪元，比 FMG 强；但『重生成回放时 locked 实体保 id、保 cell 归属、扩张时视作硬边界』这个具体协议值得写进 civ.py 重算路径——目前我们的重算是否尊重用户锁定的领土边界需要对齐这套语义。

#### 宗教生成全流程（Folk/Organized/Cult/Heresy）

**算法**：1) 每个文化生成一个 Folk 宗教（form 加权：Shamanism4/Animism4/Polytheism4/AncestorWorship2/NatureWorship1/Totemism1）。2) Organized：候选点=burg 按人口降序（不足则 s>2 的 cell 按 s 降序），quadtree 间距 (W+H)/2/N 依次收点；配比 cults=rand(1,4)/10（10-40%），heresies=rand(0,3)/10（0-30%），余为 Organized。3) expansionism：Folk=0，Organized=gauss(5,3,0,10,1)，Cult=gauss(0.5,0.5,0,5,1)，Heresy=gauss(1,0.5,0,5,1)。4) 颜色：Folk=文化色；Heresy=getMixedColor(文化色,0.35,0.2)；Cult=(0.5,0)；Organized=(0.25,0.4)。5) 若同文化出现 expansion=culture 的 Organized，Folk 更名『Old X』。

**数据模型**：Religion{i,name,type,form,culture,center,deity,expansion(global/state/culture),expansionism,color,code,origins[],lock}; id0='No religion'。

**取舍**：**adopt** — WorldEngine 完全没有宗教层——整体照搬到 civ.py 新模块。Folk=文化默认信仰、Organized/Cult/Heresy 类型学 + 『Old X』更名对小说宗教冲突叙事（正统 vs 异端）是现成的世界观生成器。

#### 宗教命名系统（神名 + 教名双语法）

**算法**：神名 getDeityName = `${Names.getCulture(c)}, The ${meaning}`；meaning 从 14 种加权模板（Number1/Being3/Adjective5/Color+Animal5/Adjective+Animal5/Adjective+Being5/Adjective+Genitive1/Color+Being3/Color+Genitive3/Being+of+Genitive2/Being+of+the+theGenitive1/Animal+of+Genitive1/Adj+Being+of+Gen2/Adj+Animal+of+Gen2）抽词表（being39 词/animal85/adjective112/genitive28/theGenitive24/color29/number12）。教名按 form 查 namingMethods 加权表选模式：Random+type / Random+ism(trimVowels) / Supreme+ism / Faith of+Supreme / Place+ism / Culture+ism / Place+ian+type(getAdjective) / Culture+type / Burg+ian+type / Type+of the+meaning；type 词按 form 再查 types 加权表（如 Monotheism→Church3/Religion2/Faith1/Creed1/Commandments1）。命名模式同时决定 expansion 标签（Place→state，Culture→culture，其余 global；无 state 时 state 降级 global）。

**取舍**：**adopt** — 词表+加权模板直接搬为数据文件；命名模式决定扩张模式这个耦合设计（『XX教区信仰』天然限于该国）很聪明，保留。后期可用 LLM 只做润色而以此做骨架，保证可重现（内容寻址缓存友好）。

#### 宗教扩张（route-aware Dijkstra + 硬边界）

**算法**：先 spreadFolkReligions：folk 宗教直接铺满其文化全部 cell（locked 宗教 cell 保留）。再对非 Folk 多源 Dijkstra：expansion=culture 时跨文化直接 return（硬边界），=state 时跨州 return；cost = cultureCost(异文化+10) + stateCost(异州+10) + passageCost；passageCost：水面有 route=50 无=500；陆地有 roads 路线=1，trail 等=biomeCost/3，无路=biomesData.cost[biome]。totalCost = p + 10 + cellCost/expansionism；上限 = cells/20*growthRate。只覆写有文化的 cell；locked 宗教 cell 不覆写。最后 checkCenters 把落在领地外的 center 迁到第一个领地 cell。

**取舍**：**adapt** — 我们已有真实路网与 travel-time——passageCost 直接换成 geoquery 的 route cost，宗教沿商路传播与 news 传播延迟共用一套图权重，两个系统互证。expansion 三档（global/state/culture）保留为作者开关。

#### 宗教起源树 defineOrigins（谱系生成）

**算法**：Folk→origins=[0]。Organized/Cult/Heresy：若同文化有 Folk 且 expansion=culture 且 each(2)(center)（center 偶数半数抽样）→origins=[folk.i]；否则从 center 做 BFS（clusterSize 步：Organized 100 步收 max2 个、Cult 50/3、Heresy 50/4），收集 id 小于自己的邻近宗教做多重起源；空则回退 folk 或 0。origins[0] 为主起源（树父），其余为次级起源（虚线）。

**取舍**：**adopt** — 宗教/文化谱系 = 小说 lore 的骨架（教派分裂史）。存进 worldstore 实体关系，配 hierarchy-tree 编辑。『只认 id 更小的邻居为祖先』保证 DAG 无环，实现极省。

#### 国家创建 createStates（首都种子）

**算法**：每个 capital burg 生成一个州：name = burg 名（<9 字符且 each5th(cell)）否则 Names.getCultureShort(culture)，再过 Names.getState 做后缀形态学；type 继承文化 type；expansionism=rn(rand*sizeVariety+1,1)；COA.generate(null,null,null,type) 生成纹章 + 文化盾形。id0=Neutrals。

**取舍**：**adapt** — civ.py 政体已从聚落生成——对齐『州名派生自首都名或文化短名+国名后缀』规则，比我们现在的命名更有味。纹章系统另议（M15 之外的低优先装饰）。

#### 国家扩张 cost-field（与文化不同的常数组）

**算法**：多源 Dijkstra 自首都：cellCost = max(cultureCost + populationCost + biomeCost + heightCost + riverCost + typeCost, 0)；cultureCost：同文化 -9（负折扣！），异文化 100；populationCost：水面 0，有人口 max(20-s,0)，无人口 5000（几乎不可穿）；biomeCost：母 biome=10，Hunting=cost*2，Nomadic 森林=cost*3，否则 cost*1；heightCost：Lake 过湖 10，Naval 过水 300，Nomadic 过水 10000，一般过水 1000，Highland h<62=1100 否则 0，h≥67=2200，h≥44=300；river/type 同文化版。totalCost = p + 10 + cellCost/expansionism；上限 = cells/2*growthRate*statesGrowthRate。水面可穿越但只有 h≥20 被占据（飞地/岛屿归属自然涌现）；不覆写锁定州与他州首都 cell。收尾把 burg.state 同步为所在 cell 的州。

**取舍**：**adapt** — 关键洞见：文化扩张与国家扩张用同一算法但不同常数（国家更受人口/文化边界约束，cultureCost=100 vs -9 使国界趋向文化界）。civ.py 政体扩张按此调参；『无人口=5000』使荒漠天然成为国界，与我们的人口场衔接。

#### 边界平滑 normalize（多数投票去锯齿）

**算法**：单遍扫描：跳过水、burg cell、邻接首都 cell、锁定州；邻居中异州陆地 cell(adversaries)≥2 且同州(buddies)≤2 且 adversaries>buddies 时，把 cell 改判给 adversaries[0]。省份版更精：统计每个 adversary 省份出现次数，改判给最高频者（competitors/max）。

**取舍**：**adopt** — 廉价的一遍元胞自动机去毛刺，球面 cell graph 直接可用。放在政体/省份扩张后处理节点，作者可开关。

#### 极不可达点 getPoles（标签锚点）

**算法**：getPolesOfInaccessibility(pack, cellId=>state[cellId]) 对每个区域求 pole of inaccessibility（距边界最远内点），存 state.pole=[x,y]/province.pole。所有标签放置与纹章定位以 pole 为锚。

**取舍**：**adapt** — 球面版：对每个政体区域用 cell graph 内部距离变换（BFS 距边界层数 × cell 边长）取 argmax，即测地『极不可达点』。这是 2D 矢量叠加层（政体标注）落球面的先决条件。

#### 政区配色 assignColors（贪心图着色 + 抖动）

**算法**：6 色基（d3 schemeSet2）：对每州找第一个与所有邻州不冲突的颜色，找不到用随机色；每次分配后 colors 数组头尾轮转。第二遍对同色州（除第一个）getMixedColor 抖动，保证同色相邻域也可区分。锁定州颜色不动。

**取舍**：**adopt** — 直接用于政体/文化/省份图层配色（React 工作台 + 球面顶点色）。比四色定理求解便宜得多且效果够。

#### 战争史生成 generateCampaigns（假历史）

**算法**：每州对每个邻居生成一场历史战役：名称 = getAdjective(80% 邻州名否则文化短名) + 加权战争类型{War6,Campaign4,Conflict2,Invasion2,Rebellion2,Conquest2,Intervention1,Expedition1,Crusade1}；start=gauss(year-100,150,1,year-6)，end=start+gauss(4,5,1,…)，按 start 排序。进行中的战争（无 end）由外交系统追加。

**取舍**：**adopt** — 喂 worldstore 时间线实体（战争=事件，有起止年），手稿一致性检查可校验『X 战争发生于 Y 年』。gauss 时间分布参数照抄。

#### 外交关系生成 generateDiplomacy（关系矩阵 + 战争级联）

**算法**：关系存 states[i].diplomacy[j] 字符串矩阵；states[0].diplomacy 是编年史(string[][])。加权表：邻国{Ally1,Friendly2,Neutral1,Suspicion10,Rival9}；邻之邻{Ally10,Friendly8,Neutral5,Suspicion1}；远国{Friendly1,Neutral12,Suspicion2,Unknown6}；跨海 Naval 对{Neutral1,Suspicion2,Rival1,Unknown1}。附庸判定：邻国 且 P(0.8) 且 面积>均值 且 对方<均值 且 面积比>2 → Vassal/Suzerain 对写；附庸复制宗主全部关系（对宗主的 Suzerain 视为 Ally）。战争循环：有 Rival 且非附庸且未在战：随机选独立 Rival，力量=面积×expansionism，若 ap≥dp×gauss(1.6,0.8,0,10,2) 开战；双方附庸自动参战；防守方盟友若 ap/dp>2×gauss 且非攻方 Rival 则毁约（关系降 Suspicion，记『Frightened by X, Y severed the defense pact』），否则参战并带其附庸；攻方盟友若对象非其 Rival 且 (P(0.2) 或 ap≤dp*1.2) 则拒战，若盟友两边都有则中立，否则参战；所有参战方两两互设 Enemy；全过程生成人类可读战报 push 进编年史。

**数据模型**：diplomacy: string[states.length]，'x'=自身/无效；9 种关系 Ally/Friendly/Neutral/Suspicion/Enemy/Unknown/Rival/Vassal/Suzerain。

**取舍**：**adopt** — 对小说套件是宝藏：关系矩阵 + 自动生成的自然语言战报（联盟背叛、附庸参战）就是剧情种子。搬进 civ.py，编年史条目写成 worldstore 事件（带年份），news 传播延迟可模拟『前线消息何时到达某城』。

#### 政体形式 defineStateForms（等级 + 文化特化头衔）

**算法**：扩张等级 tier=min(floor(area/medianArea*2.6),4)；empireMin=面积第 ceil(n^0.4)-1 名的面积，tier4 但面积不足则降 3。主形式：isTheocracy=(center 宗教 expansion==='state') 或 (P(0.1) 且宗教为 Organized/Cult)；isAnarchy=P(0.01-tier/500)；否则 Naval 州 rw{Monarchy25,Republic8,Union3}，一般 rw{Monarchy25,Republic2,Union1}。Monarchy 名按 tier 取 [Duchy,Grand Duchy,Principality,Kingdom,Empire]，再按文化 base 特化：base31 Empire/Kingdom→Khanate、base5→Tsardom、base16→Khaganate/Beylik/Horde、base12→Shogunate、base18/17 Empire→Caliphate、base18 Duchy→Emirate、base7→Despotate、base24→Satrapy；附庸特例 Marches/Dominion/Protectorate。Republic 加权{Republic75,Federation4,Trade Company4,...}，tier<2 单城邦→Free City（州名=城名时）或 City-state P(0.3)。Theocracy 按文化：欧系 Divine X/Diocese/Bishopric，希腊/罗斯 Eparchy/Exarchate/Patriarchate，尼日利亚/土 Imamah，阿拉伯系 Caliphate。fullName：17 种形容词型（Empire/Khaganate/Horde…）用 `getAdjective(name) formName`，其余 `formName of name`。

**取舍**：**adopt** — 文化→头衔映射表整个照抄（改键为我们的语族标签）。政体形式直接决定小说中的称谓一致性（『XX 苏丹国』不能一会儿是王国），worldstore 应存 name/formName/fullName 三字段同 FMG。

#### 税收与国库（salesTax/pollTax/treasury）

**算法**：按 form 默认税率：Monarchy{sales .15, poll .2}、Theocracy{.25,.1}、Union{.07,.13}、Republic{.05,.15}、Anarchy{0,0}；实际 = gauss(基准, 15%, 50%, 150%, 4)。collectTaxes：遍历 deals（交易记录），卖方所在州累计 deal.tax；再加 pollTax×(rural+urban)。编辑器有税率/国库对话框实时显示两税收入。

**取舍**：**skip** — 这是该 fork 的 deals/Markets 经济系统挂件，WorldEngine 无交易模拟且小说套件不需要账本级经济。若未来 roadmap 加入贸易，只需在政体实体上加两个税率字段即可，无算法难度。

#### 省份生成（burg 种子 + 高程成本扩张）

**算法**：每州：候选 burg 按 capital 优先、score=population×gauss(1,0.2,0.5,1.5,3) 降序；数量=max(ceil(burgs×provincesRatio/100),2)，<2 burg 不设省。formName 从州 form 对应加权表抽（Monarchy:County22/Earldom6/Shire2…），且抽中后 form[formName]+=10 自我强化——同州省份倾向同名制度（郡国 vs 领地一致性）。名 50% 用 burg 名否则文化名+getState；颜色=getMixedColor(州色)。扩张：Dijkstra 成本仅按高程 h≥70:100 / ≥50:30 / 陆地:10 / 浅水:100（深水 t=0 不可穿），州边界硬约束；maxGrowth=ratio==100?1000:gauss(20,5,5,100)×ratio^0.5。COA 以 burg 纹章为父，kinship 0.8/0.4。

**取舍**：**adapt** — civ.py 缺省份层——照此实现（我们的聚落即 burg）。form[formName]+=10 的『行政命名一致性』技巧务必保留。regenerate 用独立 Alea 种子，符合我们内容寻址缓存哲学。

#### 野省/殖民地生成（连通性判定 + New X 命名）

**算法**：州内无省 cell 循环生成 wild province：种子优先带 burg 的 cell；BFS 成本 本州陆地 3/他境 20/浅水 10/深水 30。分类：provCells 恰为整个 feature 且同岛→Island；全部 cell 在 isle group→Islands；否则 P(0.5) 且 isPassable(州中心,省中心)===false（同州陆路 BFS 不可达）→Colony。Colony 名 80% 从命名池（州名+各省名，排除已含 new 的）抽出并加 New 前缀（用过即从池删）；否则 burg 名 P(0.5) 或文化名。纹章：dominion 概率 Colony .95 / 岛 .7 / 其他 .3，dominion 时 kinship=0（独立设计）。

**取舍**：**adopt** — 『陆路不可达 → 殖民地 → New+母国地名』是极高性价比的叙事生成器（新阿斯特兰殖民地）。我们的 geoquery reachable 恰好能做同款连通性判定，直接实现。

#### 事件区域框架 Zones（11 类生成器 + usedCells 互斥）

**算法**：每类配 quantity（invasion2/rebels1.5/proselytism1.6/crusade1.6/disease1.4/disaster1/eruption1/avalanche0.8/fault1/flood1/tsunami1），实际数=gauss(q×modifier, q/2, 0, 100)，共享 usedCells Uint8Array 防叠加。各类：Invasion=进行中战役防守方边境 BFS 5-30 cell，队列 P(0.4) 取头否则取尾（锯齿形状）；Rebels=邻国边境带状 BFS 10-30（neibCellId%4!==0 且不贴边则不入队→沿边界长条）；Proselytism=组织宗教边界向邻教区 BFS（仅有人口陆地）；Crusade=某异端全部 cell；Disease=burg 起点 route-cost Dijkstra（有路 5 无路 100，预算 rand(20,40)）→瘟疫沿道路走；Disaster=burg 起点随机成本 rand(1,10) 预算 5-25；Eruption=火山 marker 起点 10-30 并把 marker 注记改成 Erupting；Avalanche=有路且 h≥70 起点沿 h≥65 蔓延 3-15；Fault=50<h<70 起点 LIFO 避河蔓延 3-15；Flood=大河(flux>(max-mean)/2+mean)有 burg 的 cell 沿同一 riverId、h<50、fl>meanFlux 蔓延 5-30；Tsunami=远洋 t=-1 起点 BFS 过 t≤2 非湖，收集 t===1 海岸 cell 10-30。命名全部『形容词化专名+加权类型词』，瘟疫名有 color/animal/adjective 三模板（Crimson Plague / Rat Fever）。

**数据模型**：Zone{i,name,type,cells[],color:'url(#hatch{n})',hidden}——cell 列表制，不落 cell 字段，可叠加于任何图层之上。

**取舍**：**adopt** — 对小说套件几乎是必抄品：zones = 有名字的临时事件区域（战区/瘟疫/灾害），天然对应我们的 news 事件源与纪元系统（zone + 起止纪元 = 事件历史）。Disease 沿路网传播与 news_arrival 用同一图，直接复用 geoquery。

#### Namesbase 马尔可夫链（音节级、单字母键）

**算法**：calculateChain：对每个源名（lowercase）扫描切伪音节：外层 i 从 -1 起，prev=name[i] 为链键（''=词首）；内层从 i+1 累积字符到 syllable（≤5 字符），空格/连字符独立成节；不拆双元音 ye 及（纯 ASCII 时）oo/ee/ae/ch；`isVowel(that)===next` 时断（保留的原版怪癖）；音节已含元音且 c+2 处又是元音时断。chain[prev].push(syllable)。getBase 生成：v=data['']，cur=ra(v)，循环≤20 次：cur===''（词尾）且长度<min 则重启；w+cur>max 则截断（<min 时仍附加）；否则下一键=cur 最后一个字符（缺键回退 data['']）。后处理 reduce：连续重复字母删除（除非在 dupl 白名单，如德语 'lt'、法语 'nlrs'）；空格/连字符后大写；'ae'→'e'；三连字母消一；词尾 '- 或空格裁掉；有 <2 字符的词段则整体连写；最终 <2 字符时从源名单随机取。chains 惰性构建、可 clearChains/updateChain 单基失效。

**数据模型**：NameBase{name,i,min,max,d(可叠字字母),m(废弃),b(逗号分隔源名)}；内置 43 基（0-31 现实语言、32-41 奇幻种族、42 黎凡特），每基 150-300 源名。

**取舍**：**adopt** — 拿来即用：纯函数、确定性（配我们的 seeded RNG 与内容寻址缓存完美），比字符级马尔可夫质量高一档而实现只有 60 行。43 个 namebase 数据文件直接搬运。civ.py 命名全线接入，culture.base 字段即链 id。

#### 国名形态学 getState（文化后缀语法）

**算法**：多词名折叠为单词首字母大写；>6 字符去尾 -berg，>5 去 -ton；base5（罗斯）去 -sk/-ev/-ov；base12（日）辅音结尾补 u；base18（阿拉伯）P(0.4) 前缀 Al；奇幻基 33-41 不加后缀。元音结尾裁剪：双元音结尾 85% 裁 2 字符，单元音 70% 裁 1，辅音结尾 40% 直接返回。后缀表：默认 ia；意/西/葡 3% terra、法 terre、德 50% land、英 40% land、北欧 30% land、希 10% eia、芬 35% maa、匈 40% orszag、土 yurt/eli、韩 guk、中 ' Guo'、纳瓦特 tlan/co、柏柏/阿拉伯 80% 'a'。validateSuffix：名尾已是后缀则不加；名尾字符=后缀首字符则删；名尾两字符与后缀首字符同元音性则删一位。

**取舍**：**adopt** — 国名≠城名≠文化名但同源——这套派生语法给小说的地名系统提供词源一致性（Astellia 国、Astel 城、Astellian 人）。getAdjective 同族。照抄进命名服务，后缀表按语族数据化。

#### 表格总览编辑器范式（table overview pattern）

**算法**：所有编辑器共用：header 每列 data-sortby + applySortingByHeader 排序；行=div.states 携带全部 data-* 属性（排序与 CSV 导出都读 dataset，不回数据层）；footer 汇总（数量/cells/面积/人口）；百分比/绝对值切换（dataset.type，百分比直接改 DOM 文本，切回时整表重渲）；行内联控件：fill-box 色块（openPicker 回调）、name input 即时写回、type/base select、expansionism number、population 点开对话框；行尾图标列 locate/fog-pin/lock/trash 默认 .hide 类只在悬浮显示。mouseenter 高亮地图区域（转 2s transition 描边或 dasharray 动画描边），mouseleave 撤销。

**编辑 UX**：统一心智模型：一实体一行、一属性一控件、growth 相关属性改动 → auto-apply checkbox 决定即时重算或攒着点 Recalculate。

**取舍**：**adapt** — React 工作台直接照此做实体表格视图（文化/政体/省份/宗教四表）。行悬浮↔球面高亮联动（拾取 id→顶点色描边）。dataset 驱动 CSV 导出这招换成 worldstore 查询即可。

#### 笔刷手动重指派（brush manual assignment + 暂存层）

**算法**：进入模式（customization=2/4/7/11）：在对应 SVG 图层 append <g id=temp> 作暂存层；viewbox 光标 crosshair，click 拾取（点地图选中该 cell 当前所属实体的表格行）、drag 绘制：半径>5 用 findAllCellsInRadius 否则单 cell，滤水面；每个 cell 画 polygon(points=getPackPolygon) 带 data-cell/data-culture 存 temp（已存在则改 attr）——改动只活在 SVG 属性里，Apply 时遍历 temp polygon 写回 pack.cells.*（连带 burg 归属），Cancel 直接删 temp。撤销 = temp.innerHTML 快照栈（≤100 条，每次 drag start 压栈）。选项：『do not overwrite existing』只画无主 cell；州模式禁改首都 cell、省模式禁改省中心（画红圈提示）。快捷键 +/-/[/] 调半径。

**编辑 UX**：笔刷语义 = 『预览可弃』：所见即暂存，数据层只在 Apply 时变更一次。

**取舍**：**adapt** — 工作台球面版：笔刷 = 球冠内 cell 集（cell graph BFS 或空间索引），暂存层 = 前端 override map（cellId→newId）叠加渲染，Apply 才提交 worldstore 带纪元。undo 用 override map 快照替代 innerHTML。这是我们 2D/球面矢量编辑最该抄的交互。

#### 中心拖拽实时重算（center drag → live recalculation）

**算法**：文化/宗教 center 画成 debug 层小圆点（r=2，文化色填充），d3.drag，drag 中 debounce 50ms：findCell(x,y) 落水则忽略，否则改 center 并（auto-apply 开启时）整体重跑 expand+redraw。释放即所见即所得。

**取舍**：**adopt** — 『拖动祖地→领土实时重新生长』是 FMG 最好的作者可控编辑之一。我们的扩张在 scipy 上跑得快，可以做同款：拖 marker → debounce → 重算该文化扩张 → 顶点色增量更新。

#### 人口编辑对话框（rural/urban 双通道）

**算法**：对话框显示 rural/urban 两输入 + 实时总数与百分比。Apply：ruralChange=new/old 有限则全体 cell pop×比例；old=0 而 new>0 则均摊 points/cells.length；urban 同理作用于 burg.population（无 burg 则禁用输入）。文化/宗教/州/省/zone 五处同一套。

**取舍**：**adapt** — 作者改『这个国家有 300 万人』→ 按比例回写 cell 人口场,这正是小说一致性需求（人口数字必须与地理一致）。实现为 worldstore 用户层 override + 场缩放操作。

#### 国家增删并合与独立（add/remove/merge/independence）

**算法**：Add state：点击地图，已有 burg 则升格为首都（已是首都则拒绝），否则新建 burg；新州 expansionism=0.5；外交行列扩展用关系变换表：对旧宗主 Enemy，其余按旧宗主关系映射 Ally→Suspicion、Friendly→Suspicion、Suspicion→Neutral、Enemy→Friendly、Rival→Friendly、Vassal→Suspicion、Suzerain→Enemy，编年史记独立宣言。Remove state：burg 降格、省份清除、军团注记删除、邻居列表清理、cells 归 0。Merge states：checkbox 选集合 + radio 选主州，军团重编号迁移、burg 首都降格、省份/cell 改归属、重画标签。Province independence：省 burg 升首都、cells 转新州、纹章 id 迁移、同款外交变换表；Release all 批量独立所有可分省。

**编辑 UX**：全部带确认对话框与不可逆警告；merge 对话框行悬浮高亮对应州。

**取舍**：**adopt** — 这些就是小说剧情操作原语：『行省独立』『两国合并』一键完成且外交关系合理演化。关系变换表照抄。在 worldstore 中实现为纪元事件（独立=新实体+关系 delta），novel 时间线自动获得事件记录。

#### 手绘州界后的省份自愈 adjustProvinces（分裂/换主）

**算法**：刷完州界 Apply 后对受影响省份：收集省内 cell 的实际州集合；若只剩一州→整省换主（新主为 0 则删除省）；否则分裂：省中心所属州保留省本体（换主则重配 getMixedColor 新色），其他州占的 cell 若 <20 个则并入 findClosestProvince（找邻接同州省份），否则 createProvince 新建省（burg cell 优先做中心，名 50% burg 名否则继承旧省名，formName 继承或 ra([Zone,Area,Territory,Province])，COA 从 burg/州派生）。

**取舍**：**adopt** — 编辑级联一致性的范本：上层实体（州界）被手改后，下层（省）自动分裂/合并而非留下悬空数据。WorldEngine 编辑管线（作者改政体边界）必须有同款自愈 pass，直接按此逻辑实现。

#### 外交编辑器（关系视图 + 矩阵 + 编年史）

**算法**：9 种关系带色板/介词短语/tooltip 定义。主视图：选中『Self』州，其余州按关系列出且地图整图按关系色重涂（statesBody path fill=relation color，halo 描边取 darker）。改关系对话框：radio 选关系 × checkbox 多选对象州（Select All/None），批量应用；对称写：Vassal↔Suzerain 互换，其余镜像。每次变更生成编年史事件：战争宣言/停战条约（停战自动补一条后续关系事件）/附庸化/竞争化/断交（『召回大使并抹除记录』）。矩阵视图：全 N×N 表格，单元格点击直接改；编年史 contenteditable 可编辑、导出 txt；Reset 单州关系归 Neutral；Regenerate 重跑生成器。

**取舍**：**adopt** — 矩阵+按关系重涂地图这两个视图搬进工作台（React 表格 + 球面重着色）。编年史事件自动生成文案的模板直接抄——它就是我们 news/事件系统的种子文案生成器。

#### Zones 编辑器（逐 cell 分解 + 擦除笔刷 + z 序）

**算法**：进入编辑时把所有可见 zone 分解为逐 cell polygon（d3 data-join key=`${zoneId}-${cell}`），画笔 enter 添加、橡皮（按钮切换/Ctrl）用 data filter+exit 删除，land-only checkbox；Apply 时按 data 分组重建各 zone.cells。行拖拽（jQuery sortable 手柄 icon-resize-vertical）改 pack.zones 数组顺序 = 渲染 z 序。zone 可 hidden（半透明行）、fog 聚焦、类型筛选下拉（同时过滤画布与表格）。新增空 zone 用 hatch{id%42} 填充。

**取舍**：**adapt** — zone=cell 列表 + 可排序叠加层的模型直接用于我们的『事件区域』：前端存 cellId 集合，球面渲染为半透明覆盖色/图案。擦除模式与 land-only 过滤保留。

#### 谱系树编辑器 hierarchy-tree（origins DAG 可视化 + 拖拽改父）

**算法**：d3.stratify 以 origins[0] 为父建树（i=0 为根），次级 origins 画虚线贝塞尔（源→靶 C 曲线 (sy*3+ty)/4、(sy*2+ty)/3 控制点）；布局 tree()，宽=叶数×50、高=深度×50；排序键=后代次级 origins 的均值（把有横向联系的分支拉近）。节点形状按类型映射（文化：Generic 圆/River 菱/Lake 六边/Naval 方/Highland 凹形/Nomadic 八边/Hunting 五边；宗教：Folk 圆/Organized 方/Cult 六边/Heresy 菱），无 cell（灭绝）者虚线描边，节点文本=code 缩写。交互：拖节点拉出带箭头虚线 dragLine 到目标节点 = 追加次级 origin（禁自环、禁指向自身后代）；点击选中→底部面板编辑 code、origins 芯片（点击删除）、Edit 打开 radio 主origin + checkbox 次 origins 选择器；updateTree 先在旧坐标更新连线再动画迁移至新布局（旧坐标映射防跳变）。cleanupOrigins 自动修复悬空引用。

**取舍**：**adopt** — 文化/宗教谱系是小说世界观的核心资产。React 版用 d3-hierarchy 或 React Flow 复刻（我们已有 React Flow 依赖！），origins 存 worldstore 关系表。拖拽改父+防环校验语义照抄。

#### Namesbase 编辑器 + 质量分析器

**算法**：选基下拉 + 源名 textarea（净化 |、/ 字符）+ name/min/max/叠字白名单输入 + 7 个示例点击重生成 + TTS 朗读。分析器：chain variety=链值数组长度均值（<15 红/<30 橙/≥30 绿）；样本量 <30 不可用/<100 低/≤400 好/>400 过多；min/max/mean/median 名长；非基本拉丁字符列表；高频叠字检测（出现 >3 次的 geminate 正则 (.)(?=\1)）建议填入 d 白名单；重复名列表；多词名占比。上传/下载 `name|min|max|d|m|names` 管道分隔格式，逐行错误报告（行号+原文+原因），支持替换或追加两种导入。

**取舍**：**adopt** — 『可编辑语料 + 即时示例 + 质量评分』的闭环编辑器是我们命名系统前端的正确形态。分析器指标（variety/样本量/叠字）照抄——它教用户如何喂好语料，减少客服型问题。

#### 州标签曲线摆放（pole 射线投射 + 最优射线对）

**算法**：对每州：从 pole of inaccessibility 每 9° 发射线（共 40 条），步长 5、起点 5、上限 300，每步检查三点（射线点 + 垂直两侧 offset 点，offset 按州 cell 数 0/5/10）是否都在州内（findClosestCell 判归属；湖泊算内部若『内湖』shoreline 全为本州或『小湖』cells≤州cells/20），失败即截断得射线长。40 条射线两两配对打分：score=(len1×angleScore1+len2×angleScore2)×curvatureScore；angleScore 按水平度分档 1/0.9/0.6/0.5/0.2/0.1（水平度=|((angle%180)-90)|/90）；curvatureScore：夹角=180 得 1、<90 禁用(0)、<120/<140/<160 得 0.6/0.7/0.8×arcSimilarity（两射线相对水平轴的对称性 1-|prox1-prox2|/90）。最优对 + pole 构成三点路径，x 逆序则翻转，d3 curveNatural 生成曲线 textPath。

**渲染**：路径存 defs>#textPaths>#textPath_stateLabel{i}，文本 <textPath startOffset=50%> 引用；DEBUG.stateLabels 时画出所有采样点（蓝=州内红=州外）。

**取舍**：**adapt** — 我们 2D 矢量叠加层缺的就是这个。球面版：在 pole 切平面内做同样的射线投射（步进沿测地线，用 cell graph 查询归属），得三点后投影到屏幕空间生成曲线标签（Three.js 上可用屏幕空间 SVG/canvas 叠加或 MVT 出图时套用原版 2D 算法）。打分表常数照抄。

#### 州标签文字适配（字号/换行/降级三段策略）

**算法**：先渲染 'Example' 量出单字符长度（getComputedTextLength/7），pathLength 换算成『字符数』。模式 auto/full/short：short→单行短名 ratio=path/name×60 clamp[50,150]；path>fullName×2→单行全名 ×70 clamp[70,170]；否则 splitInTwo 两行 ×60 clamp[70,150]，tspan dy 首行 (n-1)/-2 em。路径太短则按 mod=longestLine/pathLength 把两端点沿路径方向外推。两行标签再做边界校验：取 bbox 中心，按路径角度旋转 6 个采样点（四角+上下中点），>4 点在州内才保留，否则降级为单行（path>fullName×1.8 用全名否则短名）并重算字号 ×50 clamp[50,130]。

**取舍**：**adopt** — 与摆放算法配套照抄。『用一个 Example 文本量字宽』替代逐字排版计算，在 canvas/SVG 都成立。三段降级策略保证任何小国都有可读标签。

#### 州/省边界矢量提取 draw-borders（顶点链追踪）

**算法**：遍历 cell：与更小 id 的异州陆地邻居（或同州内更小 id 异省邻居）构成边界候选，checked 表键 `type-idA-idB-cellId` 防重；getBorder：在 fromCell 的顶点中找接触 toCell 类型的起始顶点，getVerticesLine 两轮行走（第一轮任意点→边界端点，第二轮端点→另一端点成完整链）：每步看当前顶点的 3 个邻 cell 归属 (c1,c2,c3) 与 3 个邻顶点合法性 (v1,v2,v3)，选『不是来路且两侧 cell 归属不同』的下一顶点；回到起点则闭合。链→`M x,y ...` 路径段，全部 concat 成每层单一 <path>（#stateBorders/#provinceBorders）。找到一条边界后 cellId-- 重查同 cell（一 cell 可接多条边界）。

**取舍**：**adapt** — 这就是我们『球面政体边界折线』的提取算法：geodesic cell 网格同样有顶点-cell 对偶（goldberg 顶点即三角网格重心），同款双侧归属行走可直接实现，输出球面 polyline 交给 Three.js 线渲染或 MVT。『id 大者负责画边』的去重约定照抄。

#### 州气泡图/省 treemap（比较图表）

**算法**：州：d3.stratify 平铺一层 + pack 圆填充布局，半径映射可切 area/总人口/rural/urban/burg 数（root.sum 换 accessor + 1.5s 过渡）；标签字号 = r^0.97×4/（驼峰拆分后最长词长+1），tspan 分行。省：treemap 布局按州分组（州为父节点，边框=州色 darker），标签放不下则循环截断补 '…'（≤15 次，<3 字符则清空），切指标后 setTimeout 2s 重跑截断。悬浮联动地图高亮 + 底部信息行。

**取舍**：**adapt** — 工作台用 Recharts/visx 复刻两个视图（政体对比气泡、省份 treemap），指标切换与地图联动保留。字号经验公式 r^0.97×4/maxWordLen 可以直接抄。

#### 文化 CSV 导入导出（往返编辑）

**算法**：导出读表格行 dataset 拼 CSV（含 origins 名称化）。导入：csvParse 后按 id 匹配现有文化：存在则更新（population 差额走 applyPopulationChange 按 urban/rural 比例回灌），新 id 则以随机有人口 cell 为 center 新建；origins 按名称反查还原；shield 校验白名单；namesbase 按名称 findIndex；CSV 缺失的文化全部 removeCulture（cells 归 0、burg/州引用清理、后代 origins 修复）。

**取舍**：**adapt** — worldstore 已是双层存储，等价物是实体批量导入 API；值得抄的是『人口以差额比例回灌到场』与『origins 名称化往返』两个细节，方便作者在表格软件里改世界设定再导回。

#### 宗教编辑器特有交互（灭绝显隐/神名重生成/extent 切换）

**算法**：extinct 切换：r.i && !r.cells 的宗教默认隐藏，可显形（虚线节点也出现在谱系树）。deity 行内重生成按钮调 getDeityName(culture)。expansion 下拉 global/state/culture + expansionism 数字，改动经 auto-apply 触发 Religions.recalculate()（只重扩张不重命名）。Folk 行 expansion 列锁死并给解释性 tooltip。笔刷带 protect『不覆写已有』。

**取舍**：**adapt** — 『重算与重生成分离』（recalculate=只重跑扩张保留身份）是我们 DAG 缓存粒度的 UI 对应物：扩张节点失效但命名/身份节点缓存命中。照此拆分 civ.py 的重算入口。

#### 国名编辑对话框（短名/形式/全名三段 + 生成辅助）

**算法**：短名：culture 按钮（Names.getState(getCultureShort)）、random 按钮（随机 base）、TTS；形式名：optgroup 分五类政体的 62 个预设 + 自定义输入追加；全名 regenerate 交替产出 `Adjective Form` / `Form of Name`（tick 计数器取模）；Apply 时若 formName 变更则从 optgroup label 反推 form 大类；『Update label on Apply』checkbox 控制是否重画地图标签。

**取舍**：**adapt** — 作者改名工作流的样板：生成建议 + 手工覆盖 + 是否同步渲染的选项。工作台政体属性面板按此布局，TTS 换 Web Speech API 同款即可（读音对奇幻名很实用）。

### 值得偷的技巧

- 省份 formName 抽中后 form[formName]+=10 自我强化——同一国家的省份倾向用同一种行政称谓（全是 County 或全是 Canton），一行代码实现『行政命名一致性』。
- 国家扩张的 cultureCost 同文化为 -9（负数折扣）、异文化 100，配合 Math.max(cellCost,0) 让同文化走廊近乎零成本——国界自动贴合文化界，而文化界又来自地理成本，形成三层因果链。
- 文化过水成本乘以 cells.area[i]（cell 面积），大水体自动比小海峡贵；而『只有 pop>0 的 cell 被赋文化』让荒漠/冰原天然留白成 Wildlands，边界毛边零成本涌现。
- Zones 用 P(0.4)?shift:pop 混合 FIFO/LIFO 队列制造不规则区域形状——纯队列圆形、纯栈蛇形，混合出自然锯齿。
- 瘟疫(Disease)扩散用道路成本 Dijkstra（有路 5 / 无路 100）：疫情沿贸易路线传播，与我们 news 传播延迟是同构问题，可共享 geoquery 权重图。
- 锁定宗教在重生成时保留精确数组下标，用 {name:'Removed religion', removed:true} 占位符垫齐 id 序列——以脏数据换 id 稳定性，避免全库外键更新。
- 马尔可夫链键是单个前导字母但值是多字母伪音节（≤5 字符、双元音不拆）——质量接近音节级模型，实现只有字符级复杂度；注释里明确保留了 `isVowel(that)===next` 的原版 bug 以保证生成结果向后兼容。
- 外交生成的战争级联有『盟友背叛』分支：防守方盟友在攻方压倒性优势(ap/dp>2×gauss)时撕毁防御条约并把关系降为 Suspicion，同时生成人类可读战报文本——编年史全程是自然语言字符串存在 states[0].diplomacy。
- 州标签射线检查带两个垂直 offset 探针（label 带宽），湖泊有条件算作州内部（内湖=岸线全为本州、小湖=cells≤州cells/20）——标签可以横跨自家湖泊但不会跨海。
- 笔刷撤销是 temp SVG 层的 innerHTML 快照栈（≤100 条）——改动全部暂存在 DOM 属性(data-cell/data-culture)里，Apply 才一次性写回数据层，Cancel 即删层，是极简的 staging/commit 模型。
- draw-borders 的『id 大者画边』约定 + 找到边界后 cellId-- 重查同 cell（一 cell 可能接多条边界），单遍扫描即可提取全图边界且零重复。
- 添加新国家/省份独立时的外交关系变换表（旧宗主→Enemy，宗主的 Ally→Suspicion，宗主的 Enemy→Friendly……）是一张写死的 8 行映射表，却让每次独立事件的地缘政治后果都合理。
- provinces regenerate 使用独立 Alea(seed)：regenerate=true 时换新种子否则复用全图种子——确定性重生成与我们的内容寻址缓存哲学完全一致。
- getPolesOfInaccessibility 被州和省共用为标签/纹章锚点，是所有标注系统的单一几何基础。
- hierarchy-tree 的排序键是『后代次级 origins 的均值』——有横向影响关系的分支在布局上自动靠近，减少虚线交叉。

## settlements-economy（聚落与经济：burgs/routes/goods/markets/production/military + 全部编辑器）

### 编辑 UX 模式

这组编辑器沉淀了一套高度一致、非常值得整体移植的模式语言：(1) Overview 表格范式——sortable 表头(data-sortby+dataset 数值缓存)、模糊搜索+维度下拉过滤、footer 聚合统计、百分比/绝对值一键切换（列总和归一，切回时整表重建）、CSV 导出、行内 zoom/edit/lock/delete 四件套。(2) 锁定语义贯穿一切：per-entity lock=重生成豁免（burg 组重分配、名称重掷、批量删除、路线重生成全部跳过 locked），lock-all 为『全锁则全解』的翻转开关——这正是我们 worldstore gen/user 双层的 UI 表达，应原样采用。(3) 地图交互模式用整数 customization 码互斥（14=goods 笔刷、15=市场领地、16=加市场…），进入时强制开辅助图层并打 forced 标记、退出时还原；点击放置类操作统一支持 Shift 连续执行。(4) 笔刷编辑三段式：工作副本(TypedArray 拷贝)→每笔画前快照入 undo 栈→Apply 提交/Cancel 丢弃，渲染只增量更新受影响分区。(5) 破坏性操作一律 confirmationDialog，且『重算经济』类联动做成可退订复选（good 编辑的 regenerate economy on apply、市场重生成联动 production）——编辑与重模拟解耦是作者可控性的关键。(6) 显示层微调与数据层变更严格分离（label 拖拽 vs Relocate 按钮）。(7) 默认值回退模式：市场名输入框 value=自定义名、placeholder=派生默认名，清空即回退——所有 gen 名/user 名双层字段都该这么做。(8) 批量文本往返：导出 txt→外部编辑→上传→diff 预览表→确认应用。(9) 可解释性面板（production-overview 的决策候选展开、production-chains 的链路高亮）把模拟内部状态变成作者可读的因果叙事——对小说套件而言这不是调试功能而是产品功能。整套模式在 React 工作台重做时可直接作为组件规格。

### 渲染架构笔记

整组渲染为 d3+原生 SVG，无框架：(1) 路线：每 group 一个 <g id=组名>（样式挂组级 stroke/dasharray，自定义组默认 stroke-width .5 dasharray '1 .5'），路径 d3.line+curveCatmullRom（roads/trails alpha .1，searoutes alpha .5），id=route{i}，长度直接读 getTotalLength；hover 高亮=行内改 stroke 属性、洗掉恢复 null。(2) 市场领地：getIsolines(cells→marketId) 生成每市场一条填充 path 于 #markets，编辑态复制到 #marketsTemp（fill-opacity .7）并按受影响市场增量 setAttribute('d')。(3) 商品图层：symbol/use 体系——每 good 一个 <symbol id=good-*>，cell 上 <use> + 背景 <circle fill=good.color stroke=darker(2)>；上传图标包成 200×200 symbol（raster 转 <image>、SVG 剥 inkscape 属性）。(4) 军队：#armies>g#army{state}>g#regiment{s}-{i}，团=两个 rect(主框+icon框)+emoji <text>+总数 <text>，box-size 属性驱动尺寸，旋转用 transform-origin 在团中心；基地拖拽画 line 补给线。(5) 生产链：完全字符串拼接的内联 SVG，CSS @keyframes stroke-dashoffset 做流点动画（dasharray '0.01 22'），d3.zoom 只管 viewport <g> 的 transform。(6) 贸易动画：wagon/ship marker 沿 Dijkstra 分段路径移动，动画池并发补位。(7) 聚落：#burgIcons/#burgLabels/#anchors 三层按 group 分 <g>，data-id 属性寻址；纹章 COA 渲染成 <symbol> 后 <use href=#burgCOA{i}>。对 WorldEngine 的启示：这些全是 2D SVG 平面假设（quadtree、屏幕坐标角度、getTotalLength），球面矢量叠加层需换成 Three.js polyline/sprite + 大圆插值，但『每组一容器+实体 id 寻址+增量属性更新』的 DOM 纪律和 isoline 合并 path 的分区渲染策略可平移到我们的 MVT/globe 图层。

### 功能明细

#### 首都放置 capital-placement

**算法**：候选=所有 s>0 且有 culture 的 cell。score = cells.s × (0.5+rand×0.5)（每 cell 独立抖动），按 score 降序遍历。数量=UI statesNumber，若 populatedCells < number×10 则降为 floor(populated/10)。最小间距 spacing=(graphWidth+graphHeight)/2/capitalsNumber，用 d3-quadtree find(x,y,spacing) 判重；遍历完仍不够则清空重来并 spacing/=1.2。每个首都: state=burgId, capital=1, name=Names.getCultureShort。

**数据模型**：Burg{cell,x,y,i,state,culture,name,feature,capital}; cells.burg:Uint16Array 反向索引

**编辑 UX**：无直接编辑器；burg-editor 里 capital 星标点击可换都（旧都自动降级并重分组，state.center 同步）

**取舍**：**adapt** — 映射到 civ.py 聚落模块：把 suitability(s) 换成我们的宜居度 CellField；quadtree 换成球面 cell-graph BFS 半径或 scipy cKDTree on 3D 单位向量。『抖动分数+贪心+间距拒绝+失败缩间距重试』这个骨架值得保留为快速路径。

#### 城镇放置 town-placement

**算法**：score = cells.s × gauss(1,3,0,20,3)（重尾抖动，允许低分 cell 冒头）。数量 auto = rn(populatedCells/5/(gridPoints/10000)^0.8)，或手动 manorsInput(1000=auto)。基础 spacing=(W+H)/150/(n^0.7/66)；每个候选再乘 gauss(1,0.3,0.2,2,2) 做非均匀间距；一轮放不满则 spacing*=0.5 再扫，直到 spacing≤1。已有 burg 的 cell 跳过。

**数据模型**：同 Burg；state=0（后由 states expansion 认领）

**编辑 UX**：burgs-overview 的 add-burg 模式：地图点击加城（Shift 连加），水面/占用 cell 拒绝并 tip 提示

**取舍**：**adapt** — 同上放到 civ.py；『间距逐轮减半』保证目标数量必达，比一次性泊松盘简单可控。密度公式里 (gridPoints/10000)^0.8 是分辨率归一化——我们用 cell 面积(km²)归一化替代。

#### 港口判定与聚落微移 port-assignment + bank-shift

**算法**：collectPortCandidates：对每个非锁定 burg——(a)海/湖候选：cells.haven[cell]存在且 harbor>0、水体 feature.cells>1、非 NON_NAVIGABLE_LAKE_GROUPS、温度>0（不冻）；若湖有 outlet 则 portFeatureId=Rivers.resolveLakeDrainFeature（湖港并入下游河网）；preferred=(有harbor且capital)||harbor===1(安全港)。(b)河港候选：Rivers.isNavigable(cell) 且 resolveDrainFeature 成功，一律 preferred。按 portFeatureId 分组。selectPorts：rank=(capital?-1000:0)+harbor；所有 preferred 直接入选；每个 landmass 若无入选则补 rank 最小者；总数<2 再按 rank 补足；仍<2 则该水体 0 港（『海路需要两个端点』）。promoteToPort：burg.port=portFeatureId；海港坐标移到与 haven 共享边中点的 95% 处（getCloseToEdgePoint：两共享 vertex 的中点，x0+0.95(xEdge-x0)）；河港/非港河边 burg 沿河切线的法向偏移 shift=min(flux/200,0.6)，方向按 cellId 奇偶取 ±1；无切线时按 cellId%2/r%2 轴向微移。

**数据模型**：burg.port=水体featureId(0=非港)；cells.haven/harbor 由 features 模块预计算

**编辑 UX**：burg-editor 锚形图标点击切换港口：开港时重新解析 drain feature，下游无可航水体则拒绝并警告；关港删除锚 icon

**取舍**：**adopt** — 纯图逻辑，几乎原样落到我们的球面 cell graph（haven/harbor 概念需在 civ.py 补一个 coastal adjacency pass）。湖港解析到下游干流的做法直接解决我们 geoquery route 里湖泊-海洋连通性问题；『每水体≥2港否则不设港』是很好的一致性约束，喂 travel-time 时可避免死胡同海港。

#### 聚落人口模型 burg-population

**算法**：population = cells.s/5；capital ×1.5；× connectivityRate（Routes.getConnectivityRate：基础0.8 + 每条 road/searoute 连接+0.2、trail+0.1）；× gauss(1,1,0.25,4,5) 随机化；+ ((i%100)-(cell%100))/1000 反取整微扰（避免大量相同值）；下限 0.01，rn(…,3)。显示人口 = population × populationRate × urbanization。锁定 burg 跳过 specify()。

**数据模型**：burg.population 为『人口点』，UI 换算靠全局 populationRate/urbanization

**编辑 UX**：burg-editor 人口框直接输显示人口，反除回人口点存储

**取舍**：**adapt** — 『路网连通度反馈进人口』值得抄进 civ.py（我们已有路网初版，可做 2-3 轮定点迭代：人口→路网→人口）。suitability/5 的标定换成我们气候+地形派生的承载力场。反取整微扰这种展示 hack 我们不需要（我们有真实浮点管线）。

#### 聚落文化类型判定 burg-type getType

**算法**：优先级：port→Naval；haven 指向 lake feature→Lake；h>60→Highland；有河且 flux≥100→River；(无burg或pop≤5) 且 pop<5 且 biome∈{1,2,3,4}→Nomadic；biome∈(4,10)→Hunting；否则 Generic。该 type 喂 emblem 生成和 goods/military 的 cultureType 乘数。

**取舍**：**adopt** — 简单阈值分类器，直接写进 worldstore 聚落实体的 gen 层字段（供小说一致性检查：叙述中的城市性格与地理匹配）。阈值换算到我们的高程(m)/流量(m³/s)单位。

#### 聚落特征与纹章 defineFeatures + defineEmblem

**算法**：citadel = capital || (pop>50 && P(.75)) || (pop>15 && P(.5)) || P(.1)；walls = capital || pop>30 || (pop>20&&P(.75)) || (pop>10&&P(.5)) || P(.1)；shanty = pop>60 || (pop>40&&P(.75)) || (pop>20&&walls&&P(.4))；temple = (有religion且政体Theocracy&&P(.5)) || pop>50 || (pop>35&&P(.75)) || (pop>20&&P(.5))。纹章 kinship=0.25 +0.1(capital) −0.1(port) −0.25(culture≠state.culture)，COA.generate(stateCOA,kinship,type)，type=capital&&P(.2)?Capital:burg.type。

**数据模型**：burg.{citadel,plaza,walls,shanty,temple}:0/1；plaza 由 markets 模块置位（市场中心）

**取舍**：**adopt** — 概率表直接抄为 civ.py 聚落属性生成；这些布尔特征是 watabou 契约和小说场景描写（城墙/贫民窟/神庙）的直接素材，进 worldstore gen 层。

#### 聚落分组系统 burg-groups

**算法**：defineGroup 按 options.burgs.groups 数组顺序找第一个全条件命中的组：min/max 人口点、features 精确布尔匹配（{citadel:true,walls:false,…}）、biomes 白名单、percentile（人口排位≥N%）。默认组：capital(order9,features.capital)、city(order8,percentile90,min5)、fort(citadel且无walls/plaza/port)、monastery(temple且无walls/plaza/port)、caravanserai(plaza无port,biome1-3)、trading_post(plaza,biome5-12)、village(0.1-2)、hamlet(≤0.1无plaza)、town(isDefault)。锁定 burg 若原组仍存在则不改。组决定 icon/label 样式与 watabou preview 类型。

**编辑 UX**：burg-group-editor：表格式组定义器——每行 order(渲染层级)/name(regex ^[\p{L}_][\p{L}\p{N}_-]*$ 校验+唯一性 setCustomValidity)/preview 生成器下拉/min/max/percentile/biomes-states-cultures-religions 多选弹窗(全选存空=all)/features 三态(true-false-any radio 表)/count 只读/active/isDefault(radio)/行上下移(匹配优先级)/删除；Apply 后全量重分组未锁定 burg 并 localStorage 持久化；Restore 回默认

**取舍**：**adopt** — 这是 FMG 最好的『数据驱动分类+作者可控』模式：直接搬成 WorldEngine 的实体标签规则引擎（worldstore gen 层规则→user 层锁定覆盖），React 表格实现。渲染 order 概念对应我们未来球面聚落图层的 LOD/优先级。

#### Watabou 集成契约 watabou-links（city/village/dwelling 全参数）

**算法**：【city-generator】URL=https://watabou.github.io/city-generator/?… 参数：name；population=rn(popPoints×populationRate×urbanization)；size=clamp(ceil(2.13×((popPoints×populationRate)/urbanDensity)^0.385),6,100)；seed=burg.MFCG||（地图seed+String(burg.i).padStart(4,'0')）；river=cells.r[cell]?1:0；coast=Number(port>0)；sea(仅coast且有haven)=方向编码：deg=atan2(yHaven−y,xHaven−x)×180/π，deg≤0→rn(normalize(|deg|,0,180),2)，否则 rn(2−normalize(deg,0,180),2)（0=东,0.5=北,1=西,1.5=南，注意屏幕y向下）；farms=biome∈(river?[1..8]:[5..8])；citadel；urban_castle=Number(citadel&&i%2===0)（each(2)）；hub=Number(Routes.isCrossroad(cell))；plaza；greens=plaza?1:0；temple；walls；shantytown=shanty；style='natural'；preview 链接追加 &preview=1。【village-generator】?pop&name&seed(同上)&width&height&style&tags：width 按 pop 阶梯 >1500→1600,>1000→1400,>500→1000,>200→800,>100→600,else 400；height=rn(width/2.05)；style: biome1,2→sand，temp≤5或biome9,10,11→snow，else default；tags 顺序判定：estuary(河+haven)｜island,district(haven且landFeature.cells==1)｜coast(port)｜confluence(cells.conf)｜river｜pond(pop<200且cell%4==0)；highway(connectivityRate>1)/dead end(==1)/isolated(0)；uncultivated(非可耕biome)否则 farmland(cell%6==0)；no orchards(temp≤0或>28或(>25且cell%3==0))；no square(!plaza)；palisade(walls)；sparse(pop<100)/dense(pop>300)。【dwellings】?pop&name=''&seed&tags：>200→large,tall；>100→large；>50→tall；>20→low；else small。burg.link 自定义 URL 优先覆盖一切。

**数据模型**：burg.MFCG(旧存档种子)、burg.link(自定义)；组的 preview 字段选择生成器

**编辑 UX**：burg-editor 内嵌 <object data=previewURL> 预览（每次重建 object 元素绕过 Chrome 缓存 bug），外链按钮 openURL，setCustomPreview prompt 可设任意 URL/图片

**渲染**：iframe/object 嵌入外部生成器，preview=1 是 watabou 的无 UI 模式

**取舍**：**adopt** — M15 城市生成完成前的零成本城市细节视图：在 React 工作台聚落面板直接拼同样的 URL（我们有全部输入：人口、河/海方向可从球面 cell graph 邻接算，把切平面方位角转成 watabou 的 0-2 编码）。种子=世界seed+聚落id 保证确定性、可写进 worldstore。M15 上线后此契约仍可做对照/灵感工具。

#### 路网生成主流程 route-generation（三层+Urquhart）

**算法**：三层独立生成，顺序 seaRoutes→mainRoads→trails（但 connections 记录使后者复用前者路径）：mainRoads=每个 landmass feature 内首都集合、trails=每 feature 全部 burg、searoutes=每水体 feature 的港口集合。连接候选=Urquhart 图：Delaunator 三角剖分后删除每个三角形最长边（removed[max(e,halfedges[e])]=1，比较 distanceSquared），退化情形 <2 点无边、==2 点直连。每条 Urquhart 边跑 A*/Dijkstra(findPath)，结果用 getRouteSegments 按已有 connections 切段（踏入已有路网即断，只保留新增段），段落 addConnections 双向登记。mergeRoutes 递归合并尾首相接段（nextRoute.cells[0]==thisRoute.cells.at(-1)），直到一轮合并数≤1。

**数据模型**：Route{i,group,feature,points:[x,y,cellId][],name?,length?,lock?}；pack.cells.routes={cellId:{neighborId:routeId}} 双向邻接

**取舍**：**adapt** — 对照 civ.py 路网初版：Urquhart(≈RNG图) 是比完全图 MST 更自然的道路拓扑，球面上用 3D convex hull Delaunay 或 cell-graph k近邻替代 Delaunator。『先高等级后低等级+已有连接半价』产生的干支层级正是 travel-time/news 传播需要的路网权重结构。

#### 寻路成本函数 path-cost（陆/水全常数）

**算法**：【陆】h<20→∞；habitability==0(冰川)→∞；cost=distanceSquared × (1+max(100−habitability,0)/1000)[1,1.1] × (1+max(h−25,25)/25)[1,3] × (已有connection?0.5:1) × (目标cell有burg?1:3)。【水】目标为陆：仅当 Rivers.isNavigable 且 riverEdges[current] 含 next（只能沿河道单元序列走），cost=distSq×1.5(RIVER_TYPE_MODIFIER)×conn。当前为陆入水：河 cell 必须走记录的出口边；海岸 cell 必须经 haven 出海否则 ∞。grid.temp<−4(MIN_PASSABLE_SEA_TEMP)→∞（冻海）。水水：distSq × t类型系数{-1海岸线:1, -2近海:1.8, -3开阔海:4, -4大洋:6, 更远:8} × conn0.5。isExit(水路)：到达港口只能沿河道边或从 haven 水侧进入。

**取舍**：**adopt** — 常数表直接做 geoquery route/travel-time 的快速路径成本先验（我们已有物理量：habitability→用生物群系宜居度、height modifier→坡度惩罚更准）。『海岸线便宜、远洋昂贵』的分层完美对应前现代航海；『经 haven 出海』约束避免路线穿陆，这在球面渲染矢量路线时同样关键。注意它用 distanceSquared 而非 distance——刻意让绕路更亏、路径更直，值得记住。

#### 路线几何后处理 route-geometry（锐角平滑+沿河借形）

**算法**：【锐角】对非 burg 中间点：angle=|atan2(cross,dot)|，<135°(ROUTES_SHARP_ANGLE) 时移向 (curr+2×mid)/3 或 <115°(VERY_SHARP) 时 (curr+mid)/2 —— 注意代码里更锐用 (curr+mid×2)/3、较锐用 (curr+mid)/2；新点必须 findClosestCell 仍是原 cell 才接受；接受后写回共享 pointsArray 使所有过该 cell 的路线一致弯曲。【沿河】buildRiverEdges 把每条河 cells 序列建成方向感知边表{riverId,fromIndex}；findRiverRuns 把路线中连续同河同向片段合成 run（汇流处 riverId 变化断开、共享端点）；emitRiverRun 用 meander(river.cells,…,meandering:0.5,startStep:河源h≥20?10:1) 的缓存几何按 anchorIndices 精确切片，上行时反转，路线因此贴着渲染出的河曲走。【点集】burg 所在 cell 的路径点替换为 burg 实际坐标(preparePointsArray)。

**渲染**：d3 line + curveCatmullRom.alpha(0.1)（roads/trails）/alpha(0.5)（searoutes）；每组一个 <g id=组名>，路径 id=route{i}；长度用 SVGPathElement.getTotalLength

**取舍**：**adapt** — 『路线借用河流蜿蜒几何』是我们矢量河流(λ≈11w 蜿蜒细化)管线的天然补充：让沿河道路/纤道直接采样我们河流 polyline 的弧长参数化切片，球面上同样成立。锐角平滑做进 2D 导出与未来 MVT 道路层的后处理。

#### 路线命名 route-naming

**算法**：按组加权选模型 rw({roads:{burg_suffix:3,prefix_suffix:6,the_descriptor_prefix_suffix:2,the_descriptor_burg_suffix:1}, trails:{burg_suffix:8,…}, searoutes:{…}})；后缀加权 {roads:{road:7,route:3,way:2,highway:1}, trails:{trail:4,path:1,track:1,pass:1}, searoutes:{route:5,lane:2,passage:1,'water way':1}}；burg 名取路线终点→起点→中间点倒序第一个有 burg 的 cell，转形容词 getAdjective；100+ 前缀词与16个描述词池。<4 点返回 'Unnamed route segment'。

**取舍**：**adopt** — 直接抄进 worldstore 路线实体的命名器（gen 层，可被作者锁定覆盖）；『终点城市形容词+等级后缀』的命名策略对小说中道路指称一致性（manuscript 一致性检查）很有用。

#### 路网工具函数 route-utilities

**算法**：cells.routes 邻接表上：isConnected(有任意连接)；areConnected(from,to)；isCrossroad=连接数>3 或 roads 连接>2；hasRoad；getConnectivityRate=0.8+Σ{roads:0.2,searoutes:0.2,trails:0.1}；Routes.connect(cellId)=从新 burg findPath 到最近『陆地且已连通』cell，建 trails 路线并登记；sync() 载入存档时从 routes.points 重建 connections。

**取舍**：**adopt** — connectivityRate 作为人口/市场评分特征、isCrossroad 喂 watabou hub 参数——两个廉价派生指标都进 civ.py。注意 hasRoad/isCrossroad 每次线性扫 pack.routes（find by id），我们要建 id→route 索引。

#### 商品目录与分布 DSL goods-catalogue

**算法**：68 种商品（GOODS_DATA）：raw（有 distribution+chance）、manufactured（chance:0+recipes）、hybrid。distribution 是可执行 JS 布尔表达式字符串，new Function 编译，注入方法表：random(n)%概率、nth(n)=cellId%n==0、minHabitability/habitability()(概率∝宜居)、elevation()(概率∝h/100)、biome(...)、minHeight/maxHeight、minTemp/maxTemp、shore(...t环)、type(...feature组)、river()。例：Gold='river()&&minHeight(40)'，Salt='shore(1)&&type("salt","dry")||(biome(1,2)&&random(70))||(biome(12)&&nth(10))'。recipes 为备选配方数组 Record<goodId,amount>（默认表用名字键，构建时解析成 id，未知名抛错）；biomeOutput=每 rural 人口点每周期产量（如 Grain {5:0.1,…}）；multipliers 六维乘数；demandCoverage 见需求模型。value 1(Wood)~50(Ships)。

**数据模型**：Good{i,name,tags[],value,unit,icon,color,visible,chance,distribution,biomeOutput,recipes,multipliers{cultureType,culture,state,religion,biome,zone},demandCoverage}

**取舍**：**adopt** — 整张商品表+配方图直接进 WorldEngine（小说经济一致性的骨架：某城为何富、商队运什么）。DSL 改造：不要 new Function 的任意代码执行——编译成我们 DAG 节点图的 CellField 布尔表达式（硬频率校验友好、可内容寻址缓存），条件原语与我们的 biome/height/temp/river 场一一对应。

#### 商品放置算法 goods-placement

**算法**：Math.random=Alea(seed) 确定性；cells 洗牌遍历，每 10 个 cell 重洗 goods 顺序（消除目录顺序偏置）；冰川(biome11且habitability0)跳过；对每个 good：超上限 ceil(200×totalCells/5000) 跳过→chance% 掷骰→distribution 求值→命中则 cells.good[cellId]=good.i 并 break（每 cell 一种奖励资源）。regeneratePlacement(goodId)：清掉该 good 的 cell 后单独重掷（不动其他放置、不动经济）。

**数据模型**：cells.good:Uint16Array

**编辑 UX**：goods-editor 笔刷模式(customization=14)：选中列表行后点击 cell 切换奖励资源（有则清、无则设为选中 good 并强制 visible）

**取舍**：**adapt** — 球面 cell graph 上原样可行；上限 200/5000cells≈4% 的密度常数换算到我们 4000 万 cell 时要改成按大区配额（否则单一 good 全球 160 万格）。单 good 重掷+全局锁定语义与我们 worldstore gen/user 双层完美对齐。

#### 市场创建与领地扩张 market-creation + expansion

**算法**：createMarkets：score=population×(capital?2.5)×(port?1.2)×(rand×2+0.5)，降序贪心；minSpacing=((W+H)×2)/burgs^0.6 取整，quadtree 判距，每接受一个市场 minSpacing+=1（越后越稀）。expandMarkets：多源 Dijkstra（FlatQueue），成本：BASE 10；水 +50，且水体 feature≠burg.port 再 +50；陆上跨 landmass +100(ISLAND_CHANGE)；目标 cell 属其他 state +100(DIFFERENT_STATE)；水 cell 无 good 不写入领地（但仍可穿越）。结果写 cells.market:Uint16Array，burg.market=cell 归属，市场中心 burg.plaza=1。

**数据模型**：Market{i,centerBurgId,color,name?,goods:{goodId:{stock,price}}}；pack.deals:Deal[]

**编辑 UX**：markets-overview：手工领地笔刷模式(customization=15)——pack.cells.market 拷贝为 Uint16Array 工作副本，笔刷半径滑条1-100（>5 找半径内全部 cell，否则最近 cell），每次落笔前整数组快照入 undo 栈；行选/地图点选目标市场，id=0 行=『无市场』橡皮擦；Apply 写回+同步 burg.market，Cancel 丢弃；add-market 模式(16) 点 burg 建市场（Shift 连加）；relocate 在 market-overview 点其他 burg 迁中心（保领地/存货/deal）

**渲染**：领地用 getIsolines(pack, cellId→marketId, {fill:true}) 生成每市场一条合并 path；笔刷编辑时只对受影响 marketId 重算 getVertexPath 增量更新 DOM

**取舍**：**adapt** — 市场=我们 news/贸易传播的『经济流域』概念，直接在球面 cell graph 跑同款多源 Dijkstra（成本项换成我们的路网+地形通行成本）。笔刷+整场快照 undo+apply/cancel 三段式是必抄的编辑 UX（React 工作台上任何 cell-分区手工修正都用这个模式）。

#### 市场定价与价格压力 market-pricing

**算法**：初始化两趟：raw：demand=marketPop×(consumerDemandFactor+industrialDemandFactor)，ratio=(demand+5)/(stock+5)（LAPLACE_PRICE_SMOOTHING=5），price=value×clamp(ratio,0.1,5)（PRICE_FLOOR/CEILING_FACTOR）。mfg：avgMarketCost=配方均值(Σ amount×本地市场原料价)，price=clamp(avgMarketCost+max(0,value−avgBaseCost),0.1×value,5×value)，其中 avgBaseCost=配方均值(Σ amount×原料基础value)。挂牌价：buy=mid×1.1、sell=mid×0.9（MARKET_MARGIN=0.1）。每笔成交 applyMarketPressure：price'=clamp(price+units×baseValue×0.01, floor, ceiling)（买入 units 为正抬价、卖出为负压价，MARKET_PRESSURE_FACTOR=0.01）。

**取舍**：**adopt** — 无量纲、无地图依赖，可整体照搬进 WorldEngine 经济层。Laplace 平滑+地板天花板让单周期模拟数值稳定，正是我们『冻结周期、无 tick』小说经济想要的。价格进 worldstore 供手稿一致性检查（『铁在 X 城很贵』可验证）。

#### 全球市场间贸易 global-trade（套利重分配）

**算法**：每种 good：每市场安全储备 reserve=pop×(consumer+industrial demand)×1.2（TRADE_RESERVE_FACTOR=0.2）；stock>reserve→exporter，<→importer，任一侧空则跳过。运输成本=octile 距离（dx>dy?dx+0.414dy:dy+0.414dx，市场中心 burg 直线）/mapDiagonal×DISTANCE_COST_FACTOR(1)×good.value。exporterTaxPerUnit=卖方州 salesTax×exporterPrice。unitProfit=importerPrice−(exporterPrice+transport+tax)；units=min(可出,需入)；过滤 units<0.1(MIN_UNIT) 或 totalProfit<1(MIN_PROFIT)。机会按 totalProfit 降序贪心执行，执行时重算余量再验一次；成交记 market→market deal（price=到岸成本，tax=出口税），双边 applyMarketPressure（出口方涨价、进口方跌价）+库存转移。

**取舍**：**adapt** — 框架照搬，但把 octile 直线距离升级为我们 geoquery 的真实路网 travel-time（这正是我们比 FMG 强的点——贸易成本用同一套 travel-time，与 news 传播延迟共享基础设施，小说里商队时刻表自动一致）。出口税作贸易摩擦的设计（高税国出口竞争力下降）保留。

#### 生产模拟 production-sim（worker loop + 递归配方规划器）

**算法**：produce()：collectRuralProduction（每 cell 产出入所属市场：biomeOutput×pop×modifiers + 奖励资源 min(pop×0.25,5)×modifiers；水 cell pop=陆邻居 pop 之和=渔业）→initializeMarketPrices→按人口升序遍历 burg（小城先动手）：奖励资源预置 clamp(pop×1,1,5)×modifiers 入库存→worker loop：每 tick 消耗 min(1,余量) 工人，makeProductionDecision 遍历所有可制造 good 调 planGoodAction；planGoodAction 递归：先试立即制造（每配方对库存+市场报价核可行性，score=(sellPrice×modifiers−单位原料市价)×demandMultiplier）；缺料则递归规划上游 good，可行性剪枝用 minWorkersByGood（不动点迭代：workers=1+Σ ingredientWorkers×amount 取配方最小，含 +0.001 收敛容差）算下界，超剩余工人即弃；path[] 布尔数组防配方环；plan.normalizedGain=projectedGain/workersNeeded。目标粘性：上一 tick 的 activeGoal 若 normalizedGain≥新最优则继续（防振荡；文档写 0.85 系数，代码实为 ≥ 比较——文档/代码分歧）。executeManufacture 两阶段：先规划全部原料采买（库存优先，缺口 Markets.buy，任一失败则整步放弃零副作用），后提交（扣 treasury、记 DealRecord+MfgRecord，产出=actualYield×modifiers）。loop 结束 sellInventoryToMarket 全卖（税=units×price×salesTax），burg.product=max(0,营收−原料成本)，treasury 累加。→runGlobalTrade→fillBurgsDemand：每 burg 按需求类目优先序，候选按 buyPrice/coverageWeight（单位覆盖成本）升序，预算=treasury 逐个买满。

**数据模型**：burg.production:ProductionRecord[]（LocalRecord{goodId,units}｜MfgRecord{goodId,units,recipe[],cultureModifier?,candidates?(DEBUG)}｜DealRecord{dealId}）——库存不持久化，一切可从记录+deal 日志重建

**取舍**：**adopt** — 整个单周期经济内核搬进 WorldEngine Python 层（scipy 无关，纯循环即可；4 万 burg 规模需把递归规划器 memo 化）。『deal 日志为唯一事实源+记录数组可重放』与我们 worldstore 的 gen/user、纪元语义天然契合——每个纪元重跑一个周期，作者锁定的价格/产量在 user 层覆盖。DEBUG 候选记录喂生产审计 UI 是极好的可解释性设计。

#### 需求模型 demand-model

**算法**：五类目优先序 DEMAND_PRIORITY=[food,utilities,construction,military,luxury]，目标=pop×{0.2,0.15,0.1,0.08,0.07}。consumerDemandFactor[good]=Σ_cat (good 该类覆盖/全目录该类总覆盖)×targetFactor；industrialDemandFactor[原料]=Σ 配方 amount×产成品 consumerDemand。生产时 demandFocus=第一个 shortage>0.001 的类目，demandEffect.multiplier=1+coverageWeight×shortage（缺口越大越催产）。demandGoodsByCategory 按 coverageWeight 降序、value 升序排序。

**取舍**：**adopt** — 简洁的字典序需求满足模型，直接采用；类目权重可按我们文化/政体参数微调（游牧 luxury 低等），入 civ.py 配置。

#### 州税收与国库 taxes（文档 taxes.md）

**算法**：salesTax 按政体基率 {Monarchy .15, Theocracy .25, Union .07, Republic .05, Anarchy 0}，pollTax {.20,.10,.13,.15,0}，逐州 gauss(base, base×0.15, base×0.5, base×1.5) 抖动 rn2；Neutral 恒 0。collectTaxes：清零各州 treasury→Σ deal.tax 记到卖方州（burg 卖方取其 state，market 卖方取中心 burg 的 state）→加 pollTax×(rural+urban)。无支出、无复利，冻结周期。

**编辑 UX**：States Editor 的 Treasury 列点开弹窗可改 salesTax/pollTax/treasury，带只读收入分解；改率不追溯已成交 deal

**取舍**：**adopt** — 政体→税率表进我们政体模块；国库数字是小说政治线（战争经费、饥荒赈济）的量化背书，worldstore state 实体加 treasury/taxes 字段。

#### 军事生成 military-generation（战备率+征兵+团编组）

**算法**：war alert=clamp(expansionRate×diplomacyRate×neighborsRate,0.1,5)：expansionRate=clamp((exp/Σexp)/(area/Σarea),0.25,4)；diplomacyRate=有Enemy?1:Rival?0.8:Suspicion?0.5:0.1；neighborsRate=clamp(0.5+Σ邻国对本国态度值{Enemy1,Rival.5,Vassal.5,Suzerain−.5,Ally−.2,Friendly−.1,Suspicion.1},0.3,3)。州级单位系数表 stateModifier[unitType][stateCultureType]（如 mounted×Nomadic=2.3、naval×Naval=1.8），Horde 政体 mounted×2、Republic naval×1.2，乘 alert 存 s.temp[unit]。乡村征兵：base=cells.pop/100；异文化 ÷2(Union÷1.2)、异宗教 ÷1.4(Theocracy÷2.2)、异陆块 ÷1.8(Naval÷1.2)；cell 地形类别（biome1-4=nomadic、7/8/9/12=wetland、h≥70=highland）查 cellTypeModifier 表；total=rn(base×unit.rural%×cellMod×s.temp[unit]×populationRate)；naval 仅 haven cell 且置于水面坐标。城市征兵同构：base=pop×urbanization/100，capital×1.2，naval 仅港口，用 burgTypeModifier 表。编组 createRegiments：期望团规模 expected=3×populationRate；排 platoons 升序，quadtree：20px 内可并(双方非 separate 或同 unit)直接并；否则半径 r=(expected−t)/(separate?40:20) 找伙伴；命名『{nth} ({省/城名}) Regiment/Fleet』，emblem：无兵🔰、君主制首都👑、否则最多兵种 icon；生成 note（组建年份=州 campaign 区间随机或 gauss(year−100,150,1,year−6)）。

**数据模型**：Regiment{i,a总兵,u:{unit:count},n海军旗,cell,x,y,bx,by基地,state,icon,name,angle}；options.military 单位定义{name,icon,rural,urban,crew,power,type,separate,biomes?,states?,cultures?,religions?}

**编辑 UX**：military-overview：州×兵种矩阵表，war alert 可编辑（按比值重缩放全部团并同步 SVG 文本）；单位定义表编辑器（增删单位、icon 选择、四维白名单弹窗、rural/urban%、crew、power、separate，localStorage 持久化，Apply 全量重算）；regiment-editor：兵力逐项改、split 对半、attach 合并、拖拽移动/拖基地(补给线)/黄点旋转、attack 发起战斗；regiments-overview 州过滤+合计行

**取舍**：**adapt** — 征兵系数表和 alert 公式直接进 civ.py 军事层（依赖我们已有的政体/文化/宗教场）；团实体入 worldstore（小说军事线的兵力账本）。quadtree 编组换球面空间索引。crew/power 分离（fleet crew=100）是好设计，供战斗与人口占用双口径。

#### 战斗模拟器 battle-simulator

**算法**：类型自动判定：双方海军→naval；双方纯 aviation→air；海军攻陆且带非海军单位→landing；防守方 cell 的 burg 有 walls/citadel→siege；P(0.1) 且 biome∈{5,6,7,8,9,12}→ambush；否则 field。每类型有阶段集（field: skirmish/melee/pursue/retreat；naval: shelling/boarding/chase/withdrawal；siege 攻: blockade/bombardment/storming/looting/retreat、守: sheltering/sortie/bombardment/defense/surrendering/pursue；ambush、landing、air 各有专表），阶段×兵种强度系数大表（如 skirmish ranged=2.4、machinery=3；pursue mounted=4；defense ranged=3）。side.power=Σ forces×unit.power×scheme[phase][type] / max(populationRate/10,10)。初始士气=clamp(100−(对我强度比)^1.5×10+10,50,100)−min(平均基地距离/50,15)（补给线惩罚）。每轮：attack=power×(die/10+0.4)（骰 1-6，UI 可手动重掷/手动改阶段）；总伤亡率=rand×max(两侧阶段伤亡率表{melee .2, looting/surrendering .5, blockade 0…})，按 defense/(attack+defense) 分摊；单位阵亡=Pint(u×casualties×(0.8+rand×0.4)) 上限幸存数；士气 −=伤亡×100+1。阶段转移是每类型的小状态机（如士气<25 概率 retreat/pursue；siege 中 P((powerRatio−1)/2) 开始 storming；守军械少于攻方且 P(.25)P(morale/70) 出击 sortie）。applyResults：按相对伤亡分档判词（>0.95 flawless victory…>0.4 stalemate…），战果写入每团 note（初始兵力/伤亡/团状态判词 11 档）、地图放 ⚔️ battlefields marker、生成含阶段进程记录（连续相同阶段折叠 xN）的战役 legend note；cancel 全部回滚位置。

**编辑 UX**：对话框内联可编辑一切：换类型/换阶段(弹出按钮组)/重掷骰/加援军(团选择器带距战场距离排序)/改地名与战名(文化或随机生成)/逐轮 Run/Apply/Cancel

**取舍**：**adapt** — 这是『作者可控的战斗叙事生成器』——正合小说套件：产出结构化战报（阶段流水+伤亡+judgment 词）可直接喂 RP/novelization 管线。移植成 Python 服务端模拟 + React 面板，掷骰与阶段留给作者干预，结果写 worldstore 事件实体（含 news 传播起点）。阶段×兵种系数表原样抄。

#### burg-editor 单聚落编辑器

**算法**：聚合视图：省/州、名称(输入即改+文化/随机重命名)、组、类型、文化、人口、温度(带『像地球哪里』类比 tip+温度曲线图)、海拔、7 个特征图标点击切换（port 走 drain 解析、capital 走换都事务）、production 图标行(汇总 MfgRecord+LocalRecord)、Wealth=product/pop、Treasury、watabou 预览、纹章编辑、样式区(label/icon/anchor 三个组级样式入口)、legend 笔记、锁定、删除（capital 与市场中心禁删）。Relocate 模式：点击地图搬迁（水面/占用/首都跨州拒绝），Shift 连续搬；label 可拖但仅微调（tip 提醒用 Relocate 才改数据）。

**编辑 UX**：见 algorithm；关键语义：锁定=重生成豁免；『显示层拖拽微调』与『数据层 relocate』严格分离

**取舍**：**adopt** — 整个信息架构照搬到 React 聚落面板（我们比它多：气候曲线来自真实模拟、production 来自可重放 deal 日志）。『微调 vs 搬迁』二分对应我们 user 层 offset 字段 vs 改 cell 归属。

#### burgs-overview 聚落总表

**算法**：全 burg 表：搜索(名/省/州/文化/组模糊)、州/文化下拉过滤、10 列排序(名称/省/州/文化/组/人口/product/wealth/treasury/features)，页脚均值统计；行内 zoom/编辑/锁定/删除；批量：lock-all 翻转、regenerate names(跳过锁定)、remove all(保 capital+locked)、CSV 导出(26 列含经纬度/温度类比/特征/COA JSON/watabou 链接)、批量重命名(txt 下载→改→上传→diff 预览表→确认)；气泡图：d3 stratify+pack circle packing，按州/文化/省/省+州四种分组切换带过渡动画，气泡=人口。

**取舍**：**adopt** — React 表格组件重做；『txt 往返批量重命名+diff 确认』对小说家极其实用（导出全部地名给编辑器批改再导回）。CSV 列集合就是我们 worldstore 聚落导出 schema 的现成清单。

#### route 编辑器组（creator/editor/groups/overview）

**算法**：creator：点击地图逐点加(记 x,y,cellId，显示单元格 polygon+控制点)，可删点，组下拉即时换渲染样式，完成时写 Route+双向 links，≥2 点校验。editor：控制点拖拽（drag 中实时重算 path/长度/高程剖面，drag end 才更新 cell links：旧 cell 与前后邻接解除、新 cell 建立）；点击路线在最近线段插点(getSegmentId)；点击控制点删除（<3 点禁止）或 split 模式下从该点裂成两条（共享断点 cell）；join 对话框列出同组且端点相接的候选（首尾 4 种朝向自动理顺拼接）；名称生成、长度=SVG getTotalLength×distanceScale、锁定、legend、组级样式。groups-editor：增删自定义组（强制 route- 前缀、小写下划线化、默认 stroke 样式），删组=删组内全部路线。overview：搜索/排序/锁定全部/删除未锁定/CSV。

**编辑 UX**：控制点语义三合一：拖=改形、点=删/裂、线上点=插；编辑时强制开 cells 图层辅助（forced 标记退出时还原）

**取舍**：**adopt** — 这套控制点交互语义直接搬到我们未来球面矢量叠加层的路线编辑（拖拽端只改渲染 polyline、松手才提交 cell 拓扑——与我们 gen/user 分层一致）。join 的 4 朝向归一化逻辑照抄。

#### market 编辑器组（overview/单市场/deals）

**算法**：markets-overview：市场表(名/属州/cells/burgs/stock/sales/buys/value)，value=buys−sales+Σstock×price−tax；『No market』伪行(id 0)做统计与橡皮擦；百分比/绝对切换(列总和归一)；行 hover 高亮领地、点行开单市场面板、fill-box 点击换色(增量改 SVG 不重绘)；regenerate markets(可选联动 regenerate production)与 regenerate production 分离确认。market-overview：库存表(good icon/stock/price 排序)、重命名(空=回退中心 burg 名，placeholder 显示默认)、relocate、CSV(含买卖双价)、汇总行(cells/burgs/总库存)、属州纹章展示。market-deals：deal 流水(方向 IN/OUT 徽章、对手方点击 zoom、净流色彩编码)，local(对手=burg)/global(对手=market) 过滤，净流合计，CSV。

**取舍**：**adopt** — deal 流水视图=我们经济可解释性面板的原型；『市场改名回退默认名』的 placeholder 模式抄进所有 worldstore 实体命名控件（gen 名为 placeholder，user 名为 value）。

#### goods 编辑器组（目录/单品/分布可视化编辑器）

**算法**：goods-editor：目录表(RAW/MFG 徽章、unit、produced=cell+burg 两口径、stock=市场+由记录重建的 burg 净库存、基价)；可见性复选(单个/全选/indeterminate 主控)驱动 Goods 图层；tag 过滤弹窗；producers 弹窗(产该 good 的 burg 排行,点击 zoom)；stock 弹窗(按位置分解)；比价入口；单good 删除(清 cell 放置)；restore defaults=重置目录+regenerate 全经济。good-editor：单品全字段表单——name/tags/基价/chance/unit/icon(内置 symbol 列表+上传 raster(≤200KB,包成 200x200 svg image)+上传 SVG(剥 inkscape/sodipodi 属性、剥 Noun Project 署名 text))/颜色(联动描边=darker(2))；demandCoverage 五类目弹窗；biomeOutput 逐 biome 弹窗；六维 multipliers 各自弹窗(≠1 才存)；recipes 编辑（多配方×多原料，good 下拉+数量）；『raw-only/manufactured-only』互斥提示；Apply 默认勾选『regenerate economy』（重掷该 good 放置+全经济重算），可取消只改数据。distribution-editor：可视化 DSL 构建器——OR 组×AND 条件×NOT 复选，13 种条件函数（参数类型 none/number/biomes多选/shore环多选/featureType多选），生成表达式实时显示+『命中 N cells (x%)』全图求值+人正则化自然语言解读（biome id→名、height→单位换算、&&→AND）；反向 parser：splitTopLevel 按括号深度切 ||/&&、stripOuterParens、regex 解析函数调用，不可解析则整体回退默认；右侧函数参考卡片面板。

**取舍**：**adopt** — distribution-editor 是『表达式↔可视化双向往返+实时命中计数』的教科书案例——直接用于我们 DAG 节点图里任何 CellField 布尔条件节点的编辑 UI（React 重做，命中计数在服务端对 cell graph 求值抽样返回）。『regenerate economy on apply 可退订』的编辑-重算解耦语义抄。

#### production-chains 配方图可视化

**算法**：自绘分层 DAG（无库）：节点=参与任何配方的 good；stage：无配方=0，否则不动点迭代 max(原料 stage)+1，未定型 fallback 1；连通分量(无向 DFS)按 size 降序垂直堆叠(COMPONENT_GAP 32, 虚线分隔线)；每分量内 barycenter 交叉最小化 12 轮上下扫（按上/下层邻居平均位次重排）；坐标 x=stage×(98+148)，行高 34+6。边路由：正交肘线 M-H-Q-V-Q-H（圆角 min(8,|dy|/2,max(6,dx/6))），端口在卡片高度 55% 带内均布（getPortY），肘位 x=边界基线(0.62×gap)+lane 偏移（同边界边组内按 (idx−(n−1)/2)×12 展开）+源/目标端口 spread×5+recipeIndex×0.5；同 (from,to) 多配方边合并为一条显示边带多标签。节点卡片 98×34：白底+good 色 13% 罩+双圈 icon+名称截断 12 字+基价；title=配方明细 tooltip。流动动画：每边叠加 stroke-dasharray='0.01 22' 的圆点流 path，CSS keyframes dashoffset −22 循环，速度=40px/s×max(amount,0.35)，透明度=min(0.65+amount×0.08,0.92)；默认边 30% 透明度、标签隐藏；hover 节点时 BFS 上游+下游全链高亮（其余隐藏）并启动动画。d3 zoom 0.5-2 平移缩放。箭头 marker 12 色循环，边色=(fromId×7+toId×11)%12。

**渲染**：纯字符串拼接的内联 SVG + CSS 动画，零渲染库

**取舍**：**adapt** — 我们已有 React Flow——把这套 stage/barycenter/端口-车道布局作为 React Flow 自定义布局器移植（React Flow 只给拖拽和视口），或直接抄这套自绘 SVG（依赖为零、可嵌任何面板）。『hover 高亮有向可达链+流点动画表配比』是讲解经济链给作者看的最佳形态。

#### production-overview 生产审计面板

**算法**：按 burg.production 记录数组时间序重放：LOCAL(奖励资源)/MFG(展开可见决策依据：所有候选按 score 排序，含 prep 候选的『goal sell ÷ workers × units』公式与 demand 乘数、culture 乘数徽章)/BUY/SELL(展开成交算式：units×price−tax) 四类徽章行；头部：人口、处理顺位(按人口升序排名 x of N)、所属市场、初始需求五图标、未覆盖需求(由净库存×coverage 重算)、Product/Wealth/Tax/Treasury 四色值。行点击展开详情行(display 切换)。

**取舍**：**adopt** — 『模拟决策完全可审计』是我们该抄的核心理念：WorldEngine 经济周期同样保留 per-burg 决策候选（DEBUG 开关控制体积），面板供作者理解『这城为什么穷』——直接转化为小说因果素材。

#### compare-prices + trade-details 辅助面板

**算法**：compare-prices：选 good→逐市场 stock/price 表+均价/总库存 footer+百分比模式+CSV。trade-details：贸易动画点击 batch 弹出——deals 按 good 合并(units、加权均价、value)，路径距离=Σ 段欧氏×distanceScale，卖/买双方类型徽章+端点 zoom，路径红色高亮，关闭清除。文档补充 trade-animation 本体：deal 按 (卖burg,买burg) 组 batch，状态编码 Dijkstra(cell×2+isWater)寻路，成本水1/陆5/换乘20且仅限港口 cell，分 land/water 段，陆段马车(半尺寸)/水段船 marker 逐段动画，并发池 topUp 补位。

**取舍**：**adapt** — 贸易动画=球面版『商队/船流』图层（Three.js 沿路线 polyline 移动 sprite），与 news 传播可视化共用；『点击流动物体看载货清单』对小说细节（商队运什么）价值极高。状态编码 Dijkstra(陆/水两态)抄进 geoquery 多模态 route。

#### military/regiments 编辑器组（overview/单团/总表）

**算法**：见军事生成条目的 editing_ux；补充：war alert 编辑=对该州全部团每兵种 rn(u×新旧比) 重缩放并同步 DOM；单位定义表 Apply=localStorage 持久+Military.generate() 全量重算+表头动态重建(每单位一列)；regiment split 新团 y 坐标向下探测避让(do y+=2×boxSize while 占用)；attach 合并后打开对方编辑器；battle 入场动画(⚔️ 字号 0→1000 淡出)。

**取舍**：**adapt** — 『改一个宏观参数(alert)→全军按比例重缩放且 UI 原位更新』的模式适合我们所有『标量旋钮驱动派生量』的编辑（如城市化率）。团的 base/驻地二元坐标(bx,by vs x,y=补给线)进 worldstore 军团实体。

### 值得偷的技巧

- 该 clone 是 FMG 的 TypeScript 分支且经济系统（goods/markets/production/taxes/trade-animation）远超上游 FMG——上游没有市场/生产/税收模拟；审计所得经济层是这个 fork 的原创扩展，文档 (docs/domain/*.md) 与代码高度同步，可当设计规格直接用。
- watabou sea 参数的方向编码完全非显然：atan2 角度折叠成 0=东/0.5=北/1=西/1.5=南 的 [0,2) 环形值（deg≤0 → normalize(|deg|,0,180)，否则 2−normalize），且因 SVG y 向下所以负角=北；urban_castle=citadel && burg.i%2==0（用实体 id 奇偶做 50% 确定性抽样，each(n) 模式全库通用）。
- burg 人口的『反取整』hack：population += ((i%100)−(cell%100))/1000，纯粹为了让表格排序时不出现大量并列值——生成器为 UI 服务的罕见例子。
- 路线锐角平滑会把修正后的坐标写回共享 pointsArray（points[cellId] 全局更新），使所有经过同一 cell 的路线在该点做出一致弯曲——低成本实现了『道路汇合处几何一致』。
- 沿河路线不是重新画线，而是从河流 meander 几何缓存中按 anchorIndices 精确切片（方向感知、汇流断段、上行反转）——渲染层面路河完全贴合。我们的矢量河流管线可用同样的『弧长参数化切片』喂道路。
- 水路寻路用 distanceSquared 而非 distance 作基础成本——刻意超线性惩罚绕路；且『离陆入水必须经 haven』『抵港只能从 haven 水侧』两条约束纯粹是为了渲染时路线不穿陆地。
- selectPorts 的『少于 2 港则该水体 0 港』规则：孤港无海路即无意义——这是拿拓扑约束反推经济地理的好例子。
- goods.distribution 以可执行 JS 字符串存储、new Function 编译（每 cell 每 good 一次编译，regenerate 路径倒是缓存了）；配套的可视化编辑器实现了完整的表达式↔UI 双向往返 parser（按括号深度 split）+全图命中计数+自然语言解读三联预览。
- goods 放置时『每 10 个 cell 重洗一次商品顺序』——消除目录顺序造成的商品间竞争偏置，廉价而必要。
- 生产规划器用 minWorkersByGood（不动点迭代出的配方链工人下界）做递归可行性剪枝——本质是手写的 admissible heuristic；executeManufacture 的『先全量规划采买、任一失败零副作用放弃』是事务语义。
- burg 库存从不持久化：pack.deals 全局流水 + burg.production 记录数组是唯一事实源，UI（stock 分解、生产审计）全部靠重放重建净库存——事件溯源架构，天然适配我们的纪元/重放需求。
- 经济按 burg 人口升序执行：小城先生产先买原料，大城反而后动手——配上市场价格压力，隐性地保护了小聚落不被大城吸干原料。
- 市场领地笔刷的 undo 是整条 Uint16Array 快照按笔画入栈；渲染用 isoline 每市场一条合并 path，落笔只对受影响市场增量重算——大网格分区编辑的成熟套路。
- 全球贸易的距离用 octile 近似 (dx>dy ? dx+0.414dy)——A* 八向启发式常数被挪用为『贸易运输成本』，粗但一致。
- goal stickiness 文档写 0.85 系数、代码实为 activeGoal.normalizedGain >= chosen 的严格比较——文档/代码分歧点，移植时按代码为准。
- 战斗模拟的 power 除以 max(populationRate/10,10) 做人口标度归一，使 UI 数字在任何地图人口设定下都在可读区间。
- battle-screen 的阶段状态机把叙事判词做成数据（11 档团状态、7 档战役结果、阶段进程折叠记录 xN）——输出即战报文本，几乎是为 novelization 设计的。
- market 创建时 minSpacing 每接受一个市场 +=1：市场越多间距越大，隐式实现了中心地理论式的层级稀疏。
- 文档 trade_schema 披露动画寻路是状态编码 Dijkstra（cell×2+isWater），陆/水到达同 cell 记为不同状态、换乘仅限港口 cell 且代价 20——多模态最短路的紧凑实现。

## rendering-symbology（渲染与符号系统：图标/标签/纹章/标记/贸易动画/3D视图/卫星纹理/图层栈）

### 编辑 UX 模式

FMG 编辑 UX 的可移植内核：(1) 三正交语义贯穿所有对象——lock(免疫重生成)/pin(可见性白名单)/type|group(批量样式作用域)，与 WorldEngine worldstore 的 gen/user 双层+锁模型一一对应；(2) 双视图联动——每类对象都有『地图上点击→单体编辑器(jQuery dialog 贴元素弹出)』+『总览表(排序/搜索/批量反转/CSV 导出/定位高亮)』两条入口，批量操作只在表侧；(3) 直接操纵——d3.drag 拖 marker/纹章/标签本体，标签路径用自适应密度控制点(拖=移、点=删、点线段=插)，拖拽结束才写回数据模型并重绑 cell；(4) regenerate 永远尊重锁定，且单体 regenerate 保留人工属性（纹章保盾形、marker 保位置）；(5) Ctrl+click 图层按钮直达样式编辑器、图层列表拖拽调 z-order；(6) 声明式设置面板(trade-animation-editor 的 INPUTS schema→自动 UI+重置+localStorage)是最值得抄的面板模式；(7) 内容编辑外包：富文本 TinyMCE 懒加载、纹章跳 Armoria、地牢跳 one-page-dungeon——内置简版+外链专业版；(8) AI 生成按钮内嵌在 notes 编辑器（prompt=名称+现有数据+格式约束），与我们的多厂商 AI 池即插即用。charts-overview 的『entity by metric grouped by dim』自然语言式查询构造器适合直接作为我们世界统计面板的交互原型。

### 渲染架构笔记

FMG 渲染架构是『单棵 SVG 树 + defs 复用 + 属性级样式』：g#viewbox 下 ~30 个命名图层组（完整 z-order 见 features 第 1 条），几何写进 defs（featurePaths、textPath_*、COA、charges、relief 符号库、pin/icon 符号）后全部用 <use> 实例化；样式不走 CSS class 而是直接 setAttribute，使『样式预设 JSON(selector→attrs)、组属性序列化恢复、Armoria 互操作』都成为纯数据操作。批量绘制一律拼 HTML 字符串一次 innerHTML（比 d3 enter 快），单体增删才用 d3。缩放响应集中在 invokeActiveZooming 一个函数：滤镜切换、标签/纹章可读窗口裁剪、markers 近常屏幕尺寸(size/5+24/scale)、光晕 1/scale^0.8。昂贵内容全部惰性：COA 可见才渲染、TinyMCE/three.js/控制器全懒加载。3D 侧是另一条管线：SVG 地图栅格化成纹理贴 PlaneGeometry（顶点=grid cell，水面统一高度，(h-18)/82*scale）或 SphereGeometry(equirect)；侵蚀烘焙(GPU、内容哈希缓存)提供稠密高度场与坡度/排水/海岸场；卫星纹理是单个全屏三角形 shader 把 field/coast/climate/biome 四张 DataTexture 合成瑞士晕渲风格照片（全部材质阈值被三尺度 fbm 抖动、alpha 打包水动画语义带）；水面动画经 onBeforeCompile 注入 Lambert 材质，flow 纹理供河流相位。对 WorldEngine 的总启示：我们的球面渲染应保留『图层组+组级样式+LOD 单点控制+惰性符号』四个骨架，把 FMG 的启发式材质配方嫁接到我们的真实物理场上。

### 功能明细

#### SVG 图层栈与样式预设 (layer stack + style presets, public/main.js + modules/ui/style-presets.js + layers.js)

**算法**：z-order（自底向上，全部为 g#viewbox 下的 <g>）：ocean(oceanLayers+oceanPattern) → landmass → texture → terrs(oceanHeights+landHeights) → lakes(freshwater/salt/sinkhole/frozen/lava/dry 六个子组) → biomes → cells → gridOverlay → coordinates → compass(display:none) → rivers → terrain(relief 图标) → relig → cults → regions(statesBody+statesHalo) → provs → zones → borders(stateBorders+provinceBorders) → routes(roads/trails/searoutes) → temperature → coastline(sea_island+lake_island) → ice → goods(goodsCells/goodsIcons/goodsBurgs, 默认隐藏) → markets → tradeAnimation → prec → population(rural+urban) → emblems(burgEmblems/provinceEmblems/stateEmblems, 默认隐藏) → icons(burgIcons+anchors) → labels(states/addedLabels/burgLabels) → armies → markers → fogging-cont(mask=url(#fog)) → ruler → debug；legend 挂在 svg 根而非 viewbox。图层开关：每层一个 toggleXxx 按钮，layerIsOn=按钮无 .buttonoff class；turnButtonOn/Off 同时调 getCurrentPreset() 同步预设下拉。图层预设（political/cultural/religions/provinces/biomes/heightmap/physical…）= toggle 按钮名数组，存 localStorage，自定义预设可保存。z-order 可拖拽重排：jQuery sortable 的 #mapLayers 列表，moveLayer 用 getLayer(id) 把按钮映射回 svg 组，insertAfter/insertBefore 真实移动 DOM。样式预设：12 个系统 JSON(default/ancient/gloom/pale/light/watercolor/clean/atlas/darkSeas/cyberpunk/night/monochrome) + localStorage 自定义(前缀 fmgStyle_)；JSON 结构为 {CSS选择器: {属性:值}}，applyStyle 逐属性 setAttribute（值为 null 则 removeAttribute）；#burgLabels/#burgIcons/#anchors 前缀的选择器额外写进全局 style.burgLabels[group] 缓存供重绘恢复。

**数据模型**：样式预设 = flat JSON: selector→attr map（如 '#burgIcons > g#town': {fill,size,stroke,…}）；全局 style 对象缓存每个 burg 组的全部属性；图层预设 = string[] of toggle ids；localStorage keys: presetStyle / preset / fmgStyle_*。

**编辑 UX**：Ctrl+点击图层按钮直接打开该层样式编辑器（editStyle）；图层列表可拖拽调 z-order；预设修改后自动变 custom 并可另存。

**渲染**：全部是 <g> 分组 + 属性级样式（不是 CSS class），使得序列化/恢复/预设切换都是纯 attr 操作；display:none 做隐藏，删内容做真正卸载。

**取舍**：**adapt** — WorldEngine 2D 矢量叠加层（河流/边界/聚落还没上球）正需要一个图层模型：把这份 z-order 清单直接当作我们 Three.js 球面 overlay 的图层顺序参考（ocean→terrain→hydro→admin→routes→symbols→labels→annotations）。样式预设的 selector→attrs JSON 模式很适合搬成我们 React 工作台的 styleDoc（按 layerId→props），支持 localStorage 自定义 + 内置主题；『每个 burg 组的属性缓存后随重绘恢复』的思想对应我们按 settlement-tier 分组样式。

#### 缩放感知 LOD 控制器 (invokeActiveZooming, main.js:540)

**算法**：单一函数在每次 zoom 后统一处理所有随缩放变化的显示逻辑：(1) 海岸线滤镜三段切换：scale≤1.5 用 dropShadow，1.5<scale≤2.6 无滤镜，>2.6 用 blurFilter（组上 auto-filter 属性开关）；(2) 标签字号 relative=max(rn((desired+desired/scale)/2,2),1)（期望字号与屏幕不变字号的均值），当 relative*scale<6 或 >60 时加 .hidden；(3) 纹章按 font-size*scale 判定，<25px 或 >300px 隐藏，且只有可见且未渲染(children[0] 无 href)时才触发 renderGroupCOAs 惰性渲染；(4) 州界光晕宽度 = data-width / scale^0.8，<0.1 整层隐藏；(5) markers rescale: zoomedSize=max(rn(size/5+24/scale,2),1)，重设每个 marker svg 的 width/height/x/y；(6) 标尺文字 = rn(10/scale^0.3*2,2)。

**数据模型**：各组上的 data-size/data-width/auto-filter/rescale 属性承载 LOD 参数。

**编辑 UX**：hideLabels/hideEmblems/rescaleLabels 为全局复选项。

**渲染**：关键技巧：昂贵内容（COA）只在『缩放后屏幕尺寸落入可读区间』时才首次渲染——按需填充 <use> 的 href。

**取舍**：**adopt** — 直接搬到球面渲染：按相机距离/屏幕投影尺寸做统一 LOD pass（标签渐隐区间、纹章 25–300px 可读窗口、光晕 1/scale^0.8 衰减公式都可原样用）。惰性符号渲染（占位 use + 可见时才生成）对应我们按 tile/视锥体惰性生成 settlement 图标纹理图集。

#### 聚落图标层 (draw-burg-icons.ts)

**算法**：按 options.burgs.groups（含 order 排序）为每个组建 <g>，组上 data-icon 指向符号（默认 #icon-circle）；组内每 burg 一个 <use href=icon x=b.x y=b.y id=burg{i} data-id={i}>，用 innerHTML 批量拼串写入（非 d3 enter，为了速度）。港口 burg 另在 #anchors 同名组下加 <use href='#icon-anchor'>。重绘前把现有各组的全部 attributes 序列化进全局 style.burgIcons[group] 再删组，新组恢复样式（缺省 fallback 到 town 组样式）。提供单 burg 增/删函数（drawBurgIcon/removeBurgIcon），组缺失时退化为全量重绘。

**数据模型**：burg{group, port, x, y, i, removed}; options.burgs.groups[{name, order}]; style.burgIcons/anchors[group]=attr map。

**编辑 UX**：样式全部在组级（改一组=改该 tier 所有城镇），单体编辑走 burg editor。

**渲染**：<use> 实例化 + 组级属性 = 千级图标零成本换肤。

**取舍**：**adapt** — 映射到 civ.py 聚落层上球渲染：按 tier(capital/city/town/village) 分组的 instanced billboard（Three.js InstancedMesh），组级样式=uniform，anchors 的『港口叠加副图标』模式照搬（我们已有 harbor 信息）。样式序列化-恢复机制对应重生成后保持作者样式。

#### 聚落标签层 (draw-burg-labels.ts)

**算法**：与图标层同构：按组建 <g>，组上 data-dx/data-dy（单位 em）给整组统一偏移；每 burg 一个 <text x=b.x y=b.y dx dy text-rendering=optimizeSpeed>{name}</text>。组样式经全局 style.burgLabels 缓存/恢复；单体增删同图标层。

**数据模型**：同上；标签文本直接取 burg.name。

**编辑 UX**：标签相对图标的偏移在组级调（em 单位随字号缩放）。

**渲染**：text-rendering:optimizeSpeed 显式牺牲质量换性能。

**取舍**：**adapt** — 球面标签：SDF 文本 billboard，按 tier 组化 + em 偏移概念保留（偏移随字号缩放，避免图标遮挡）。与上面的 LOD 窗口结合。

#### 纹章层与碰撞布局 (draw-emblems.ts)

**算法**：三个层级各自算基准尺寸：state startSize=minmax((graphH+graphW)/40,10,100)，statesMod=1+n/100-(15-n)/200（15 国≈50px）；province /100, minmax 5–70, mod=1+n/1000-(115-n)/1000（≈20px）；burg /185, minmax 2–50, mod=1+n/1000-(450-n)/1000（≈8.5px）。每个 COA 节点 {x,y,size(个体倍率),shift=tierSize*size/2}，anchor 优先用 coa.x/y（作者拖过的位置），否则 state/province 用 pole（极点）、burg 用坐标。三层节点合并跑一次 d3.forceSimulation：alphaMin .6, alphaDecay .2, velocityDecay .6, 只有 forceCollide(radius=shift)，.stop() 后同步 tick n=ceil(ln(alphaMin)/ln(1-alphaDecay)) 次（确定性、不动画）。输出 <use data-i x=x-shift y=y-shift width/height={size}em>，组 font-size=tierSize ⇒ em 尺寸随组缩放。renderGroupCOAs：遍历组内 use，COArenderer.trigger(id,coa) 惰性生成 defs 内 svg 并回填 href。

**数据模型**：coa 挂在 state/province/burg 上：{...heraldry, size?, x?, y?, custom?}; size=0 表示隐藏。

**编辑 UX**：配合 emblems-editor 拖拽写回 coa.x/y。

**渲染**：font-size(px) + width in em 的组合是『组级基准尺寸 × 个体倍率』的优雅实现；碰撞松弛保证三层纹章互不重叠。

**取舍**：**adopt** — 确定性同步 forceCollide 布局（算好 tick 数一次跑完）可直接用于我们球面上的任何符号防重叠（切平面局部坐标跑 2D 碰撞即可）；tier 尺寸随数量的调制公式照抄。纹章本身进 worldstore 实体的 emblem 字段。

#### COA 纹章生成器概述 (generators/emblems/generator.ts + 数据表)

**算法**：输入 parent coa + kinship(亲缘概率) + dominion(附庸概率) + type(文化/地形类型)。流程：t1 底色 P(kinship) 继承否则按 tinctures.field 权重抽（metals3/colours4/stains P(.03)/patterns1）；93% 加 charge（有 pattern 时 50%）；36% 加 ordinary（lined 类 30–50% 概率、可继承 parent 的第一 ordinary）；division 概率决策树：charge+ordinary→3%、charge→30%、ordinary→70%、都无→99.5%（rare ordinary 3%）。Rule of Tincture：getTincture 在 metals/colours 间强制交替，argent+or 都用过则禁 metals，while 循环避开 base 和已用色。charge 选择：P(kinship-0.1) 继承 parent、type 映射表 30%（Naval→anchor/lymphad…，共 River/Lake/Highland/Naval 等），否则 charges.types 权重（conventional33/crosses13/beasts7…共 ~20 类 300+ charge）。位置系统：字母网格 p∈'a'..'z'+'ABCDEFGHIJKL'(bordure 环带)，positions.conventional/complex/divisions[division] 权重表；尺寸 getSize：e=1.5（bordure 下 1.1）、位置串越长越小（>10→0.18）。特殊结构：bordure 内 95% 加中心 charge；inescutcheon 80% 内嵌小 charge(size .5)；perPale/perFess 直线分割 30% dimidiation（divided=field/division 各半）；40% counterchange；perCross/perSaltire 50% 放 4 charges；dominion 时加 canton(小方块)+父徽 charge(p='y',size .5) 并从 a/j/y 位清出空间。pattern 命名 `{pattern}-{t1}-{t2}[-size]`，semy 递归选 charge。2% sinister/reversed 翻转。渲染器(renderer.ts)：shield path 做 clipPath，division 是模板 path 二次 clip，charge 逐 SVG 文件 fetch 后作为 defs <g> 复用，patterns 生成 <pattern>，最后叠 backlight 径向渐变+描边。

**数据模型**：Emblem{t1, shield, division{division,t,line}, ordinaries[{ordinary,t,line,divided}], charges[{charge,t,t2,t3,p,size,sinister,reversed,divided}], custom}. 纯 JSON、可序列化、可传 Armoria URL。

**编辑 UX**：custom=true 表示用户上传图片替换，生成器不再碰。

**渲染**：全部 <use>+clipPath+pattern 组合，单个 COA ≈ 几 KB DOM；只进 defs，一处渲染处处引用。

**取舍**：**adapt** — 按计划做简化版：保留 Emblem JSON 模式（可序列化进 worldstore 政体实体，gen 层生成、user 层锁定覆盖）、Rule of Tincture、kinship/dominion 继承（配合我们政体谱系正好：附庸国 canton 承父徽的叙事语义对小说套件极有用）。charge 库可先用 20 个常用 SVG；位置字母网格和尺寸表照抄。

#### 地物路径/海岸线渲染 (draw-features.ts)

**算法**：对每个非 ocean feature：顶点链→simplify(容差 0.3)→clipPoly(画布裁剪,margin 1)→fractalizeCoastline(按 feature.i 做种子的分形扰动)→round path + 'Z'。产物写入 defs：#featurePaths 存 <path id=feature_{i}>；#land mask（lake 填黑、island 填白）与 #water mask（相反，底为全白 rect）各自用 <use> 引用同一 path；#coastline 按 sea_island/lake_island 分组、#lakes 按 group(freshwater 等) 分组，同样全是 <use>。一条几何路径被 5 处引用（路径本体、两个 mask、海岸线描边、湖面填充）。

**数据模型**：feature{i,type:ocean|island|lake,group,vertices[]}。

**编辑 UX**：无直接编辑；样式按 lake group/island group 分组调。

**渲染**：defs+use 去重是核心：几何只算一次，mask 用于 ocean pattern/texture 的裁剪。

**取舍**：**already_better** — 我们的海岸线来自 IGM 物理模拟（真实等高线），无需分形伪装；但『单几何多引用(mask/描边/填充共享 path)』的 SVG 组织值得在 equirect 导出器里采用；湖泊六分类(freshwater/salt/sinkhole/frozen/lava/dry)的符号学分组值得纳入我们水体分类（气候模拟可以真算出 salt/dry/frozen）。

#### 商品生产层 (draw-goods.ts)

**算法**：三个子层。goodsCells：两遍扫描——先累计每 cell 可见商品产量求全局 max，再按 opacity=0.1+0.9*normalize(total,0,max) 给每个 cell 的每种商品画一个同多边形 polygon（fill=商品色，透明度=产量强度，多商品叠色）。goodsIcons：每 cell 主商品(cells.good[i]) 画 <use href=#icon>（可选底圈 circle r=size/2, stroke=Goods.getStroke(color)），size 默认 6。goodsBurgs：burg 上方『名牌』——常数组 PLATE_ICON=3, FONT=3.5, GAP=.2, ENTRY_GAP=.8, PAD_X=1, PAD_Y=.6, RX=1, FILL=#f5f5f5，charWidth=1.2，全随用户 data-size 等比缩放；对每 burg 的产出做容量 3 的插入排序保 top-3，entry 宽=icon+gap+数字宽(位数*charWidth)+0.4*font*0.62，圆角 rect 底板水平居中于 burg，内排 圆圈+图标+数值文本。

**数据模型**：pack.goods[{i,name,color,icon,visible}], cells.good(主产), Production.getCellProduction/getBurgProduction。

**编辑 UX**：toggleGoods Ctrl+click 开样式编辑；商品 visible 过滤集。

**渲染**：拼 HTML 字符串一次性 innerHTML；透明度全局归一化让跨地图可比。

**取舍**：**adapt** — 我们经济层未建，但该层为『资源/产出可视化』给出完整方案：cell 产量→透明度归一化 choropleth + burg top-3 名牌。名牌(plate)模式尤其适合小说套件的『城镇速览卡』（在球上 hover 城市浮出产出/人口牌）。搬到 Three.js 用 canvas 纹理 sprite。

#### 地图标记渲染 (draw-markers.ts)

**算法**：每个 marker 渲染为嵌套 <svg id=marker{i} viewbox='0 0 30 30' width/height=zoomSize x=x-size/2 y=y-size>（锚点在底部中心，像地图钉）。zoomSize=rescale? max(rn(size/5+24/scale,2),1): size。内容三层：pin 形状（13 种：bubble/pin/square/squarish/diamond/hex/hexy/shieldy/shield/pentagon/heptagon/circle/no，全是硬编码 30×30 path，参数化 fill/stroke）、emoji 文本 <text x=dx% y=dy% font-size=px>（icon 非 URL 时）、或外链图片 <image>（icon 以 http/data:image 开头时）。pinned 属性开启时只画 pinned 标记。

**数据模型**：marker{i,icon(emoji|url),x,y,type,dx,dy(百分比定位),px(图标字号),size,pin,fill,stroke,pinned,hidden,lock,cell}。

**编辑 UX**：见 markers-editor；dx/dy 用百分比在 30×30 视箱内微调 emoji 对中。

**渲染**：嵌套 svg 当定位容器：内部坐标系永远 30×30，缩放只改外框 width/height——图钉内容零重排。

**取舍**：**adopt** — POI 标记直接进 worldstore 实体渲染层：13 种 pin 形状库照搬（SVG path 可直接转 sprite 图集），emoji-as-icon 是零资产成本方案，'size/5+24/scale' 的近常屏幕尺寸公式换成相机距离版本。pinned（只显示钉住的）语义对作者聚焦写作场景很实用。

#### 标记程序化生成器 (markers-generator.ts)

**算法**：配置驱动：34 种类型各带 {icon(emoji), dx/dy/px 显示微调, min(候选数下限), each(每多少候选出 1 个), multiplier, list(pack)=>候选 cell 数组, add(id,cell)=>写 notes}。数量公式 getQuantity：candidates<min/multiplier→0，否则 ceil(len/each*multiplier) 封顶 len。选点：从候选数组均匀随机 splice（无放回）；occupied[cell] 全局防叠（跨类型）。坐标：cell 有 burg 则吸附 burg 坐标。候选谓词示例：volcano h≥70；hot-spring h>50 且有文化；bridge 有 burg+burg 人口>20+有河+flux>全河平均+非海岸；inn pop>5 且 Routes.isCrossroad；lighthouse harbor>6 且邻 cell 是有航线的水；waterfall 有河 h≥50 且邻 cell h<40 有河；sacred-mountain h≥70 且所有邻 cell h<60（孤峰）；portal 只取前 10% burg（fantasy 文化集才启用 multiplier=+isFantasy）。add 函数生成 notes{id:'marker{i}',name,legend}——大量加权表(rw)/随机表(ra)/P() 概率拼 flavor 文本：inn 名=颜色|形容词×动物、菜=烹法×原料、酒=类型×酒种；battlefield 关联 state.campaigns 并 generateDate；statue 用 5 种古文字 Unicode 随机拼 40–100 字铭文；dungeon 嵌 one-page-dungeon iframe（seed=地图seed+cellId 确定性）；encounter 嵌 deorum.vercel.app/encounter/{cellId}。regenerate 语义：lock 的保留并重新占位，未锁的删 DOM+删 notes 后重跑生成。

**数据模型**：marker 数组 + notes 数组（id 关联）；config 可 get/set（编辑器可改 each/multiplier）。

**编辑 UX**：锁定即免疫重生成——与手工添加并存。

**渲染**：生成与渲染完全解耦（生成只写数据+notes）。

**取舍**：**adopt** — 这是本组对 WorldEngine 产品价值最高的功能：谓词式 POI 生成器直接映射到我们 cell graph（谓词换成我们的字段：h→elevation, flux→河流水力属性, Routes→路网, culture→civ 层），产物进 worldstore gen 层实体（lock=user 层覆盖，正好对上我们的 gen/user 双层+锁模型）。notes 的 name+legend 生成喂小说一致性检查与地名库；『外部服务确定性嵌入(seed=世界seed+cell)』模式可用于我们未来的城市生成预览。min/each/multiplier 三参数量控制公式照抄。

#### 市场区域层 (draw-markets.ts)

**算法**：以 cells.market[cellId] 为分类函数调 getIsolines(pack, getType, {polygons:true}) 提取每个市场辖区的等值线多边形，用 curveBasisClosed 平滑成闭合 path。填充+描边分离：fill path + border path(clip-path 引用同 path 的 clipPath，stroke-width .7 → 裁剪后成品是只描内侧的边界线，视觉宽 .35)。中心 burg 画徽记：circle r=max(rn(baseRadius+1/scale,2),2)（近常屏幕大小）+ emoji ⚖️ 文本（data-icon 可换）。hover 高亮：克隆 fill path 为 .highlight，transition 1s 到 fill-opacity .7+红描边，mouseout 600ms 退场后 remove。常数：MARKET_RADIUS=3, MARKET_FONT=5。

**数据模型**：pack.markets[{i,color,centerBurgId,name?}], cells.market（cell→市场 id）。

**编辑 UX**：hover 高亮联动（highlightMarketOn/Off 也被贸易面板调用）。

**渲染**：clipPath 内侧描边技巧；isoline 平滑掩盖 cell 锯齿。

**取舍**：**adapt** — 对应我们 civ 层任意 cell 分类字段的区域可视化（政体/文化/教区/市场圈）。球面版：cell 分类边界提取我们已能做（cell graph 边界边），平滑用球面样条；『内侧描边』改用 shader 边界距离场。hover 双向高亮（图⇄表）是工作台需要的交互。

#### 军团符号层 (draw-military.ts)

**算法**：每 state 一个 <g fill=stateColor color=darker(stateColor)>；每 regiment：主 rect 宽=size*4（海军 n=true）或 size*6，高=size*2，中心文本=Military.getTotal(reg)（兵力数），左侧再一个 h×h 的 rect(fill=currentColor 即深色) 内放 icon（emoji 文本或外链 image 二选一，与 markers 同判定）；整组 transform=rotate(angle) 且 transform-origin 设在 reg 坐标。moveRegiment：位移动画 duration=hypot(dx,dy)*8ms, easeSinInOut，五个子元素各自 transition 到新位置。box-size 由 #armies 组属性控制。

**数据模型**：regiment{i,name,icon,x,y,angle,n(海军),state,units…}。

**编辑 UX**：拖动/转向在军事编辑器（不在本组文件）；moveRegiment 供『行军』操作调用。

**渲染**：currentColor 继承 hack：组上 color=深色, 图标底块 fill=currentColor——每州只算一次深色。

**取舍**：**skip** — WorldEngine 暂无军事模拟且小说套件近期不需要兵棋符号；若未来做战役时间线，可回头借『距离×8ms 移动动画』和 NATO 式兵力牌思路。emoji/图片双模式图标判定逻辑已在 markers 处采纳。

#### 地形符号层 (draw-relief-icons.ts)

**算法**：对每个陆地 cell（h≥20，跳过有河的）：h<50 走生物群系图标（biomesData.iconsDensity[biome]==0 跳过），h≥50 走山丘图标。biome 分支：density=iconsDensity/100，泊松盘半径 radius=2/density/全局density，先以 P=iconsDensity*10 概率丢弃整 cell，poissonDiscSampler 在 cell 包围盒内采样、polygonContains 过滤出界点；图标高 h=(4+rand)*size，grass-1 额外 ×1.2；图标从 biomesData.icons[biome] 随机，conifer 且 temp<0 换 coniferSnow。山地分支：radius=2/density；type: h>70&&temp<0→mountSnow, h>70→mount, else hill；尺寸 h>70→(h-45)*mod（mod=0.2*size），hill→minmax((h-40)*mod,3,6)。全部图标按 y+s 排序（画家算法，近处后画）后一次 innerHTML 输出 <use>。三套图标集：simple(#relief-{type}-1，旧图标名折叠映射), colored(#relief-{type}-{variant})，gray(-bw 后缀)；variant 按类型随机（mount 2–7, hill 2–5, cactus 1–3…）。

**数据模型**：biomesData.icons[biome]=类型数组, iconsDensity[biome]=0–100；图标是 defs 内符号库。

**编辑 UX**：组属性 density/size/set 三个旋钮。

**渲染**：y+size 排序实现伪 3D 遮挡；雪线由温度场驱动（snow 变体）。

**取舍**：**adapt** — 作为我们 equirect/局部地图导出的『手绘风格图层』：泊松盘 + polygonContains + 画家排序算法照搬（我们球面 cell 有精确多边形），密度由真实生物群系与坡度驱动（我们有真坡度，比 FMG 的高度阈值好）。信标地面视角渲染不用它，但小说插图导出需要。

#### 卫星纹理程序化烘焙 (draw-satellite-texture.ts::generateSatelliteTexture)

**算法**：单个全屏三角形 fragment shader 把 4 张 DataTexture 合成『卫星照片』RGBA 渲染目标：uField(R/G=16bit 高度 hi/lo、B=ridge/gully 打包 detail/0.4+0.5、A=drainage，NearestFilter+shader 内手动双线性 decodeHeight=(r*65280+g*255)/65535)、uCoast(R=模糊陆地掩码 0.5=真海岸线、G=水面高度、B=河道掩码含真实河宽、A=湖泊类型码*40)、uClimate(R=temp+128、G=prec/30、B=网格高度=水深，水下高度先做 3 遍邻域平滑)、uBiome(RGB=生物群系反照率、A=植被密度，13 类内置表如热带雨林[.11,.36,.13]d=1.0、冰川[.93,.95,.97]d=0)。材质合成顺序（全部阈值被 fbm 噪声 breakup(×220)/macro(×9)/patch(×38) 抖动防等高线感）：植被色×clump 聚簇→草地掺 GOLD 枯草补丁→排水线 riparian 变暗变绿(smoothstep(.1,.7,drainage))、强水线掺 SEDIMENT→中坡露 DIRT(smoothstep(ROCK_SLOPE_LO-.35..+.15)) 且 gully 聚集→陡坡 ROCK：ROCK_SLOPE_LO=.65 起露、HI=1.35 全岩、CLIFF=2.2 转暗色，strata 层理 sin(h*70)、干热区(scorch=smoothstep(20,28,tempC) 且低湿)转红棕 ROCK_DRY，高频细节按坡度淡出(防拉伸条纹)→海滩：水面上 SAND_BAND=0.022 高度带内平地，warm=smoothstep(2,14,tempC) 决定沙/砾→永久雪：tempC<-5 带(gully-2.5 修正=雪聚沟壑)双尺度抖动、坡>2.4 脱落、植被扣 45%→凹凸腔调 1-gully*.28+ridge*.16→瑞士式烘焙晕渲：nrm 由中心差分梯度，暖光(1.16,1.10,.97)/冷影(.84,.88,1.03) 双色调→高山空气透视→轻度调色(饱和1.1, gamma .94)。水体：bathymetry 驱动 SHELF→OCEAN→ABYSS 渐变，近岸 lagoon(暖 turquoise/冷 steel)+海底沙光+破碎浪 foam 线(避开河口 riverWater)；六种湖泊按 lakeCode=round(A*6.375) 分支：freshwater 浅缘 FRESH_RIM #a6c1fd、salt 乳白+蒸发盐壳环、sinkhole 天坑青→深蓝、dry 龟裂粘土(cracks=1-smoothstep(0,.05,|breakup|))、lava 玄武岩壳+发光裂缝、frozen 冰盖+压力裂纹。河流：riverMask smoothstep 成水道，陡坡(slope+breakup>0.55..1.5)白水激流，tempC<-5 结冰(与雪线同带)。Alpha 打包动画掩码：陆地1、河0.45(冰河→1)、封闭湖0.7、开阔水0+岸边提示≤0.3、干/岩浆/冰湖=1。输出超采样至 field 尺寸 2 倍上限(仅锐化程序噪声)，WebGL2 才开 mipmap。uSlopeScale=(scale*100/82)/texel 世界坐标换算。

**数据模型**：输入=erosion-bake 的 {pixels,coast,cols,rows} + grid 气候数组 + pack.cells.biome(经 g 映射回 grid)；输出单张 THREE.Texture 直接当 material.map。

**编辑 UX**：一个开关 + 分辨率下拉；bake 内容寻址缓存使重开免重烘。

**渲染**：所有中间纹理用完即 dispose；seed 归一进 uniform 保证同地图确定性。

**取舍**：**adapt** — 整套材质合成配方是给我们球面渲染的现成答案：我们有真实坡度/侵蚀/排水/温度/降水场（不需要它的 bake 伪装），把这个 shader 的分层配方(坡度阈值三段岩石、抖动阈值防等高线、雪聚沟壑、干热红岩、瑞士双色调晕渲、六类湖泊符号色)移植进 globe_mesh 的顶点色/纹理管线即可大幅提升观感。lake 类型码打包进通道、alpha 承载动画带的『单纹理多语义通道』设计照搬。

#### 河流流向相位纹理 (generateRiverFlowTexture)

**算法**：1024px 级 canvas，对每条河调 Rivers.addMeandering 取蜿蜒折线，沿途累计弧长 dist；每段用 createLinearGradient 编码 R=127.5+127.5*sin(d*k), G=…cos(d*k)（k=2π/FLOW_WAVELENGTH, 波长10地图单位）——sin/cos 双通道使相位在双线性插值下无锯齿断裂（atan2 解码）；段再细分到波长/5 以内使线性渐变逼近圆相位。B 通道=40+steep*215 打包覆盖+沿程陡度：drop=(heightAt(a)-heightAt(b))/subLen（从烘焙高度场采样，scale=82 抵消常数偏移），steepness=clamp((drop-0.6)/2.4,0,1)；关键技巧 steep=max(new, steep*0.55)——陡度瞬升缓降，让白水拖出瀑布下游的泡沫裙。线宽=max(2*Rivers.getOffset(flux…), 4/scale)，比渲染河道更宽（由卫星纹理 alpha 带门控显示范围，所以低分辨率够用）。

**数据模型**：输入 pack.rivers（cells+points+widthFactor+sourceWidth）；输出 CanvasTexture(LinearFilter, no mipmap, flipY 默认)。

**编辑 UX**：无（随卫星纹理自动生成）。

**渲染**：画布黑底，无河=B<0.1。

**取舍**：**adopt** — 我们已有矢量河流+水力宽度(w=7√Q)+蜿蜒(λ≈11w)，这个『弧长相位 sin/cos 编码 + 陡度指数衰减』是让河流在球上动起来的最廉价方案：把同样的 stroke 编码画进 rhombus-atlas 的流向纹理，shader 端 atan2 解相位做下行波。瀑布=陡度信号我们可直接从真实高程差算，比 FMG 更准。

#### 水面动画着色器注入 (view-3d-renderer.ts::applyWaterAnimation)

**算法**：material.onBeforeCompile 在 MeshLambertMaterial 的 #include <map_fragment> 后插入 GLSL：按 diffuseColor.a 分带——waterMask=1-smoothstep(.30,.38,a) 为海洋：双 octave 值噪声反向漂移(wp=vUv*(140,100)，n1 速度(0.6,0.25)、n2 2.3 倍频反向)组合波 waves，crest=waves^4 做稀有阳光闪点，swell 低频正弦涌浪 ±0.025，岸边 surf 由 alpha 的 shore hint(0.02–0.3) 驱动正弦拍岸；lakeBand=smoothstep(.64,.69)×(1-smoothstep(.71,.78)) 封闭湖：只有慢速平静涟漪(±5%)无浪无闪光；riverBand=(.36,.42)×(1-(.50,.58)) 河流：flow 纹理解相位 flowPhase=atan(R-.5,G-.5)，行波 sin(flowPhase-t*2.2*speedMul+texNoise*2.5)*.6+二次谐波*.4（谐波取整数倍才能跨 sin/cos 回卷无缝），speedMul=1+steep*2；注释明确强调：所有运动必须来自相位方向（噪声场只能静态空间变化，否则全图统一漂移方向会让逆向河流看起来倒流）；steep>0 时瀑布翻滚：tumble/boil 两组相位锁定脉冲(5×/3× 谐波, 16/11 rad/s)+时间散列 shimmer 原地沸腾，froth 向白色 mix（重浪=泡沫）+splash 峰值 pow 溅光。最后 diffuseColor.a=1.0（alpha 是掩码不是透明度）。驱动：requestAnimationFrame 循环仅在卫星纹理激活时运行，uTime 共享对象。

**数据模型**：复用卫星纹理 alpha 带 + flow 纹理；无额外几何。

**编辑 UX**：无独立 UI（随 satellite 开关）。

**渲染**：单 pass、零顶点开销的全水系动画。

**取舍**：**adopt** — 『alpha 语义带 + onBeforeCompile 注入』直接适配我们 Three.js 球（顶点色管线可加一个 water-class 顶点属性或纹理），海洋/湖/河三带动画参数照抄。注释里『运动只能来自相位、噪声必须静态』的教训是踩坑结晶，值得写进我们的渲染文档。

#### 3D 网格视图与侵蚀烘焙集成 (view-3d-renderer.ts 主体 + view-3d.ts 控制器 + 3d-view.md)

**算法**：经典网格：PlaneGeometry(graphW,graphH, cellsX-1, cellsY-1)，顶点 z=getMeshHeight：陆地 (h-18)/82*scale（LOWER_BY_WATER=18 让浅海压到 0 以下），水/岸顶点统一取所属 feature 水面高（湖用 feature.height）——经 gridToPack 映射查 feature。纹理=把 2D SVG 地图经 ExportMap.getMapURL('mesh',{noLabels,noWater,noViewbox,fullMap}) 栅格化到 resolutionScale²canvas。可选 Loop subdivision 1 级平滑（外部库懒加载）。侵蚀模式：GPU bake（外部模块，key 含高度图+河+参数的内容哈希缓存）产出稠密高度场，PlaneGeometry 段数=erosionDetail(256–2048 长边)，顶点直接采样 ErosionBake.heightAt；bake 分辨率阶梯：detail≥2048→4096 等 + satellite 需求取 max，封顶 min(GPU maxTexture, 8192)；失败回退经典网格+警告。卫星纹理与侵蚀独立（关侵蚀时跑 zero-strength bake 只为坡度场）。光照：AmbientLight(lightness)+SpotLight(castShadow, 2048 shadowmap)；时间预设 dawn/noon/evening/night 各带 sun xyz/sunColor/lightness/sky/waterColor，手动改任何值回落 custom（匹配检测：位置精确相等+lightness 差<0.05）。天空模式：背景色+Fog(500,3000)+10 倍尺寸水平面 y=-3。相机 MapControls(damping .05, polar 0..π/2, distance 50–1000)。渲染按需（controls change 触发），水动画激活时才连续 rAF。导出：JPEG 截图、OBJExporter。3D 标签：canvas 画文本→SpriteMaterial billboard，quality=40/80 超采样、25% 高度余量；burg 图标=CylinderGeometry 圆片+竖连接线（材质/几何按组缓存）；高度取 raycaster（经典网格）或 heightAt（稠密网格无 BVH 不能逐标签 raycast）；可见性=render 后节流 200ms 批处理：dist<100*size 且 >6*size。Globe 模式：SphereGeometry(1,64,64)+等距圆柱纹理：宽=resolutionScale，高=宽/2，地图按 latT/latN 计算 dy 偏移贴进画布，非全球地图先铺内嵌 base64 云层图再叠地图；星空背景 external png；OrbitControls。

**数据模型**：options.threeD 全套（见 view-3d-options.ts 默认值：scale50, resolutionScale4096, erosionStrength30, riverDepth10, octaves2…），会话内存不持久。

**编辑 UX**：控制器懒加载 renderer 模块；O 键开关设置面板；侵蚀滑杆用 change 而非 input（每次动值即 GPU 重烘）；heightmap 编辑器持有小窗 3D 预览自动刷新；view-only 原则：3D 永不改地图数据。

**渲染**：SVG→raster→贴图的桥接使 2D 样式系统免费复用到 3D。

**取舍**：**already_better** — 我们是测地球原生 3D（真实球面网格+顶点位移），不需要 plane mesh/equirect globe 这套。但要偷四件事：(1) time-of-day 预设+『手动改动回落 custom』的匹配检测 UX；(2) 标签 billboard 的距离双阈值裁剪+200ms 节流批处理；(3) 稠密网格『不 raycast 改采高度场』的标签落地策略（我们放大细节层同理）；(4) view-only 铁律与内容哈希 bake 缓存（我们 DAG 已有内容寻址，把渲染烘焙也纳入）。

#### 比例尺 (draw-scalebar.ts)

**算法**：长度取整：val=init(100)*size*distanceScale/scaleLevel，>900 取整到千、>90 到百、>9 到十、否则到一；再换算回像素 length=val*scaleLevel/distanceScale。绘制：白色衬线+深色主线双线制造浮雕感、第三条 stroke-dasharray='{size} {length/5-size}' 生成 5 等分刻度齿、6 个刻度文本(最后一个带单位)、可选 data-label 标题；背景 rect 按 getBBox+四向 padding 自适应。fitScaleBar：位置=屏幕百分比(data-x/y 默认 99%) 减自身 bbox。

**数据模型**：distanceScale(单位/px)、distanceUnitInput、scaleLevel(当前缩放)。

**编辑 UX**：点击打开单位编辑器。

**渲染**：dasharray 当刻度生成器是个小技巧。

**取舍**：**adopt** — 1-10-100-1000 nice-number 取整算法照抄进我们 2D 导出图与球面 HUD；球面上要按视中心纬度的真实地面分辨率算 distanceScale（我们有真实行星半径，比 FMG 的均匀假设准）。

#### 贸易动画：路径规划 (trade-animation.ts)

**算法**：deals 按 (卖方burg,买方burg,local|global) 聚合成 batch（market 方解析为其 centerBurg）。寻路：在 cells.routes 邻接表（cell→{邻cell:routeId}）上跑双状态 Dijkstra，状态 id=cell*2+isWater（水路=route.group=='searoutes'），边权 WATER=1, LAND=5, 换乘 SWITCH=20（惩罚频繁上下船）；FlatQueue 优先队列，Float64Array dist + Int32Array prev 双数组回溯。路径几何：逐边从 route.points（带 cellId 标签的渲染折线）里定位 fromCell→toCell 段，向两端扩展取完整 cell 运行段，支持反向；拼接时跳过上一边已输出的 fromCell 段全部点（只跳 1 个点会在蜿蜒水路上产生 180° 折返抖动——注释里点名的 bug 教训）；首尾点吸附 burg 坐标（河港 cell 中心≠burg 位置）。分段：按水/陆切段，段间共享边界点。路径缓存 Map by 'start-end'，无路径的 batch 从池中剔除。并发模型：目标并发数 concurrent(默认30)，每条完成后 topUp 补位，generation 计数器使 stop 后的回调全部失效。

**数据模型**：TradeBatch{id,deals[],startBurgId,endBurgId,type}; TradePath{points,segments[{type:land|water,points}]}。

**编辑 UX**：trigger(batches) 支持从贸易面板点单笔交易立即演示。

**渲染**：无 DOM，纯数据层。

**取舍**：**adopt** — 双状态 (cell, mode) Dijkstra + 换乘惩罚直接并入我们 geoquery 的 route/travel-time（现在只有单模式）：陆/水/换乘成本参数化后，news_arrival 的信使模型立即多式联运。路径几何吸附与段拼接的坑（跳过整个共享 cell 段）值得直接抄注释。

#### 贸易动画：绘制 (draw-trade-animation.ts + trade-animation-editor.ts)

**算法**：ship.svg/wagon.svg fetch 后转成 defs <symbol>（一次性）。每段动画：Catmull-Rom(alpha .1) 生成 path，getTotalLength 后按 ~1px 间隔懒采样并缓存（getPointAtLength 很贵，points 数组按需填充）；attrTween 里 t→(idx,frac) 线性插值位置，朝向角=当前段 atan2 与下一段角度差插值（±π 回卷归一化）避免转角跳变；duration=length*durationMs(默认250ms/单位)，陆路 ×landDurationModifier(5)，段间 setTimeout segmentChangePause(1000ms)；wagon 尺寸=ship/1.6。透明 circle(r=minmax(size,2,6)) 做点击热区→打开 TradeDetails；highlight() 画红色半透明路径。编辑器：INPUTS 声明式表格（select+slider-input 自定义元素），改值即写 options.trade.animation + localStorage + TradeAnimation.restart()，每行独立重置按钮；处理 slider-input 内部控件冒泡重复事件（e.target!==e.currentTarget 忽略）。

**数据模型**：options.trade.animation{displayType,concurrent,duration,landDurationModifier,segmentChangePause,markerSize}。

**编辑 UX**：声明式选项 schema→自动生成 UI+重置+持久化，是 FMG 里最干净的设置面板模式。

**渲染**：SVG transform 动画（非 canvas），依赖 d3 transition。

**取舍**：**adapt** — 为小说套件做『消息/商队传播可视化』：在球面上沿 route 大圆段动画化 news_arrival 的传播（Three.js 沿曲线移动 sprite，角度插值照搬）。声明式设置 schema 模式抄进 React 工作台的面板生成。

#### 纹章编辑器 (emblems-editor.ts)

**算法**：armiger 三级级联选择：State→Province→Burg 下拉互相过滤（选州刷新省列表，选空省回落州徽，中立 burg 单列），当前对象类型高亮；点地图纹章 target.parentNode.id 反查类型。操作：换盾形（重触发 COArenderer）；单体尺寸滑杆 0–5（重建该 use，shift=组font-size*size/2）；拖拽移动（d3.drag 改 use x/y，end 时写回 coa.x/y=左上+shift）；regenerate 用 COA.generate(parent, kinship=0.3, dominion=0.1) 且保留原盾形（parent 链: burg→province|state, province→state）；Armoria 双向：JSON.stringify(coa) URL 编码跳转 azgaar.github.io/Armoria?coa=...&from=FMG；上传：raster 直接 dataURL、SVG 清洗（去 inkscape/sodipodi 属性、adobe pgf 节点）后 base64 内联，包成 <svg viewBox='0 0 200 200'><image/></svg> 插 defs，coa={custom:true,保留 size/x/y}；下载 SVG/PNG/JPG（canvas 栅格化 0.92 质量，JPG 白底）；整图 HTML 画廊导出：先 Promise.allSettled 渲染全部 COA，再生成带锚点导航的静态 HTML（州→省→市三级 figure 链接）。

**数据模型**：coa JSON + custom 标志；画廊纯字符串模板。

**编辑 UX**：生成物可整体交给专业外部编辑器再回流（Armoria roundtrip）——『内置简版+外链专业版』的分工模式。

**渲染**：所有预览走同一 defs 内 COA。

**取舍**：**adapt** — 纹章编辑进我们工作台时：级联 armiger 导航对应 worldstore 政体→省→聚落层级查询；regenerate(kinship,dominion) 保盾形 = gen 层重掷+user 层锁字段的范例；Armoria URL roundtrip 免费获得专业编辑器（Emblem JSON 兼容即可）。HTML 画廊导出直接可用于小说附录（族徽图鉴）。

#### 标记编辑器 + 总览表 (markers-editor.ts + markers-overview.ts)

**算法**：编辑器：核心语义是 type 作用域批量编辑——改 icon/pin/尺寸/颜色时 getSameTypeMarkers()（同 type 全部标记）一起改并逐个原地重写 DOM（redrawIcon 改 text/image 属性，redrawPin 重写 g.innerHTML），type 为空则只改自己；拖拽 end 时反推 marker.x=x+dx+zoomSize/2, y=+zoomSize（从 svg 左上角还原锚点）并 findCell 重绑 cell；lock 开关；『加同类』按钮进入连续放置模式。总览表：每行 icon/type/编辑/定位(zoomTo x,y,8,2000ms + highlightElement)/pin/lock/删；表头排序(applySortingByHeader)、按 type 搜索过滤、行数统计 n of total；批量：invertPin（全体取反并设置 #markers[pinned] 属性触发过滤重绘）、invertLock、removeAll（跳过 locked，连带删 notes）、regenerate（Markers.regenerate 保 locked）；新增标记的类型选择菜单来自 Markers.getConfig()；CSV 导出含 note name/legend（双引号转义）与 lat/lon 换算。

**数据模型**：同 markers-generator；notes 以 'marker{i}' id 关联，删除标记必须同步过滤 notes。

**编辑 UX**：『类型=样式作用域』+『锁定=免疫重生成』+『pin=可见性白名单』三个正交语义；overview 表格与地图双向联动（定位/高亮/编辑）。

**渲染**：所有编辑都是原地 DOM patch，不全量重绘。

**取舍**：**adopt** — worldstore POI 管理界面的完整 UX 蓝本：type 作用域批量样式、lock/regenerate、pin 过滤、表格总览+搜索+批量反转+CSV 导出（我们加 epoch/有效期列）。React 表格实现更容易，语义照搬。

#### 标签编辑器：路径控制点系统 (labels-editor.ts)

**算法**：标签是 <text><textPath href=#textPath_{id}> 结构。编辑时在 #debug 层重建控制点：沿 path 每 l/max(ceil(l/200),2) 弧长放一个 circle(r2.5)，加上可点击的 path 副本；拖 circle→收集全部 circle 坐标→curveNatural 样条重生成 d 写回 textPath 与副本；点 circle 删点、点 path 在最近两点间插点（距离平方排序取前二，索引小者+1 处 insert）；拖 text 本体= translate 整体（控制点组同步 transform）。文本：'|' 分行→tspan dy=1em、首行 dy=(n-1)/-2 em 垂直居中；随机名：stateLabel 用州文化 Names.getState，否则 bbox 中心 findCell 取该 cell 文化的地名。数值区：startOffset 20–80%、相对字号 30–300%、letter-spacing 0–20px（改字号/字距后需重设文本触发回流）；『拉直』按钮把 path 替换为过 bbox 中心的水平线 M{cx-w},{cy}h{2w}。组管理：下拉换组（appendChild 移动）、新建组（名字 slug 化、唯一性/首字母校验；旧组只剩 1 个元素时改为原地重命名）、删组（states/addedLabels 只清内容不删组）；states/burgLabels 禁止换组。

**数据模型**：路径存 #deftemp 的 textPath_{id}；lastSelectedGroup 记忆用于新建标签默认组。

**编辑 UX**：控制点密度自适应（约每 200px 一个）；hover 提示区分拖点/删点/加点；所有操作即时预览。

**渲染**：curveNatural 保证过点平滑；文本沿任意曲线流动是 SVG textPath 独有优势。

**取舍**：**adopt** — 区域标签（国名/海名/山脉名）沿曲线排布是地图审美刚需：我们球面版用测地曲线控制点+贴地 SDF 文本，控制点交互模式（拖/点删/近邻插点、约200px 自适应密度）1:1 照搬到工作台。'|' 分行与相对字号百分比语义也保留。

#### 注记(Notes)编辑器 (notes-editor.ts)

**算法**：notes=[{id,name,legend(html)}] 以元素 id 关联任意对象（marker、label、burg…）。打开时按 id 查找或新建；富文本用 TinyMCE 从 CDN 动态 import（失败带随机 hash 重试破缓存），Change 事件回写 note.legend；元素选择下拉可遍历全部 notes；『定位』按钮 highlightElement，元素已不存在则提示删除孤儿 note；AI 生成：prompt='Respond with description. Use simple dry language. Invent facts... format to HTML' + name + 现有 legend，结果直接覆盖 legend；notes 全量 JSON 下载/上传；pin 控制 hover 信息框是否常驻。

**数据模型**：全局 notes 数组，与 markers/labels 的 id 约定耦合（删对象须删 note）。

**编辑 UX**：id-关联而非对象内嵌，使 notes 可独立导入导出；AI 按钮就在编辑器内一键扩写。

**渲染**：notesBox（hover 浮层）与编辑器共享数据。

**取舍**：**already_better** — 我们 worldstore 实体本身就是结构化 notes 的超集（gen/user 双层、锁、纪元有效期），且产品核心就是喂手稿一致性。要吸收的只有两点：孤儿 note 检测 UX（对象删除后提示清理）和『AI 扩写按钮嵌在实体编辑器里』的位置（我们有多厂商 AI 池，直接接）。

#### 小地图 (minimap.ts)

**算法**：零渲染成本方案：<svg viewBox='0 0 graphW graphH'><use href='#viewbox'/></svg>——直接引用主地图整个内容组；因 #viewbox 自带当前缩放 transform，minimap 给 use 施加逆变换 translate(-viewX/scale,-viewY/scale) scale(1/scale) 还原全图。视口矩形=屏幕范围反投影 clamp 到图界。点击=getScreenCTM().inverse() 转图坐标后 zoomTo(x,y,scale,450ms)。窗口用注入 <style> 覆写 jQuery dialog padding。

**数据模型**：无自有数据。

**编辑 UX**：点击平移；随主视图 updateMinimap() 同步。

**渲染**：<use> 引用整棵渲染树 = 免费镜像（浏览器共享渲染缓存）。

**取舍**：**adapt** — 球面版做『定位小球/迷你 equirect』：渲染主 globe 的低分辨率副本相机即可（Three.js 第二 viewport，等价于 use 引用思想——共享场景不复制数据），点击反投影 setView。若 2D 视图回归则 SVG use 技巧原样可用。

#### 数据图表工作台 (charts-overview.ts)

**算法**：维度×指标×分组的通用聚合引擎。7 个维度(states/cultures/religions/provinces/biomes/markets/goods)各带 getId(cellId,contribution)/getName/getColors/landOnly，goods 维度声明 requires:'good'；19 个指标：scalar 型带 quantize(cellId)→值（人口=pop*populationRate、urban=burg.population*rate*urbanization、面积、cell 数、高程 mean/max/min、温度、降水、海岸/河流 cell 计数、burg 利润），contribution 型带 getContributions(cellId,ctx)→[{value,good}]（产值=units*good.value、产量），prepare() 每次渲染算一次上下文（biomeProduction）；aggregate=sum/mean/max/min，stringify/formatTicks 分离（tooltip 全文 vs 轴刻度 si 缩写）。兼容性校验：维度 requires 的 tag 不在指标 provides 里→报错拒绝；不可堆叠指标(mean/max/min)强制 groupBy=entity。聚合：双层 dict dataCollection[entityId][groupId]=values[]，excludeNeutral 跳 id 0。图表：d3 stack + stackOffsetDiverging（普通）或 stackOffsetExpand（归一化%），横向条形图，高=实体数*25+边距，y 轴宽=最长名*7px，图例行数按可用宽度回算；排序三模式（value=按实体总和、name、natural）。每图 figure 附 CSV/SVG/PNG(2x 白底栅格化)/删除按钮；charts 列表随 mapId 换图重置；1–4 列网格布局。

**数据模型**：ChartOptions{entity,plotBy,groupBy,sorting,type,excludeNeutral} 可序列化。

**编辑 UX**：一行式查询构造器『{entity} by {metric} grouped by {dim} sorted {mode}』读起来像自然语言；指标 hint 用 info 图标而不占下拉宽度。

**渲染**：纯 D3 生成独立 SVG（自带 viewBox，导出即成品）。

**取舍**：**adopt** — 作者侧『世界统计』面板的现成设计：维度/指标注册表直接映射我们 CellField（DAG 输出天然就是 cellId→值），contribution+requires/provides 的 tag 机制解决『按商品拆分』类多值指标——照搬接口。React 里用同一 d3 代码或 visx 重写皆可；CSV 导出喂小说数据核查。

### 值得偷的技巧

- 河流流向纹理用 R/G=sin/cos(弧长相位) 双通道编码，使相位场经双线性插值后仍可 atan2 无缝解码（单通道 sawtooth 会在回卷处断裂）——这是把矢量流场塞进纹理的教科书技巧。
- 瀑布检测的陡度信号做非对称衰减 steep=max(new, steep*0.55)：陡度瞬升缓降，自动在落差下游拖出『泡沫裙』，一行代码模拟瀑布水雾拖尾。
- 卫星纹理的 alpha 通道不是透明度而是语义带（陆1/湖0.7/河0.45/海0+岸边提示0.3），一张纹理同时携带颜色与动画掩码，shader 末尾强制 a=1。
- 水动画注释里的反教训被写成规则：『所有运动必须来自相位方向，噪声只能静态』——UV 空间漂移的噪声会让全图水流朝同一方向，逆向河流看起来倒流。
- 纹章防重叠用 d3 forceSimulation 但同步跑：tick 数 n=ceil(ln(alphaMin)/ln(1-alphaDecay)) 预先算出，确定性布局、无动画帧。
- COA 惰性渲染链：drawEmblems 只放空 <use>，invokeActiveZooming 检测『可见且 25–300px 可读』才触发 COArenderer 生成 defs——数千纹章零启动成本。
- 组级 font-size(px) + <use width='1em'> 的 em 联动：改一个组属性等比缩放全组符号，个体 size 只是 em 倍数。
- markers 用嵌套 <svg viewBox='0 0 30 30'> 当定位容器：内部坐标恒定，缩放只改外框，图钉锚点(底部中心)由 x=x-w/2, y=y-h 约定实现。
- dungeon 标记的 legend 内嵌 watabou one-page-dungeon iframe，seed=地图seed+cellId——外部服务的确定性联动，免存储获得无限内容。
- 重绘前把每个 SVG 组的全部 attributes 序列化进全局 style 对象再删组重建——样式在数据重生成后自动恢复，作者调校不丢失。
- minimap 是一个 <use href='#viewbox'> 引用整棵地图渲染树+逆 transform，零复制零重绘成本。
- 比例尺刻度用 stroke-dasharray='{size} {length/5-size}' 一条线生成 5 等分刻度齿。
- 贸易寻路的状态编码 stateId=cell*2+isWater，换乘惩罚 20 vs 水1/陆5；路径拼接注释点名『只跳一个共享点会让商船在蜿蜒水路上 180° 折返抖动』的实测 bug。
- 16bit 高度打包进 R/G 字节且必须 NearestFilter，shader 内先解码四邻再手动双线性——硬件插值打包数据会产生灾难性错误值。
- 生物群系纹理采样 uv 加噪声 wobble(1.6/gridSize)，让生态区边界脱离 cell 网格游走；气候纹理按 cell 中心半texel 对齐。
- 3D 标签可见性在 render 后 200ms 节流批处理（doWorkOnRender），距离双阈值 dist<100*size 且 >6*size 同时裁太远与太近。
- 文件清单中的 trade-animation.ts 与 draw-trade-animation.ts 都存在且分工明确（规划/绘制分层）；本组所有列出文件均存在，无缺失。

## app-platform（应用平台与 IO）

### 编辑 UX 模式

FMG 的编辑器平台层核心是「全局模式旗标 + 懒加载对话框控制器 + 实体墓碑」三件套。(1) 全局 customization 旗标：进入任何重量编辑模式（高度图 ERASE/笔刷等）时置 1，save/autosave/export 全部被硬拒绝（"cannot be saved in EDIT mode"），保证存档永远是干净状态——这是极廉价但极有效的一致性护栏。(2) 51 个编辑器全部是 dialog controller，经 Proxy 注册表懒加载（首次调用才拉 chunk），约定每模块只导出一个方法对象（StatesEditor={open}）；架构文档强制 build-on-open / destroy-on-close（隐藏≠关闭，关闭必须删 DOM+听器+定时器），大列表要求窗口化渲染——这是他们从「所有面板预烤进 index.html 导致会话内存爬到 GB 级」的教训中总结的。(3) 实体编辑语义：数组元素 0 永远保留（neutrals/wildlands/No religion），删除实体不删元素只打 removed 墓碑（保 id 稳定），lock:true 表示「重生成时不动它」——锁定/重生成语义与 WorldEngine worldstore 的 gen/user 双层+锁完全同构，FMG 用单 flag 实现了我们用双层实现的东西。(4) overview（只读表格）与 editor（可变）严格分离命名；每个域普遍配平「单实体编辑器 + 全量 overview 表格 + 分组编辑器」三视图（Burg/BurgGroup/BurgsOverview、Route/RouteGroups/RoutesOverview）。(5) 加载后完整的引用完整性自修复（load.ts 数百行 data integrity）代替 schema 校验，宽进严修。

### 渲染架构笔记

渲染架构：单一巨型 SVG（#map > #viewbox），main.js 启动时按固定 z 序 append 约 40 个 <g> 图层（ocean→landmass→texture→terrs→lakes→biomes→cells→rivers→terrain→relig→cults→regions(statesBody+statesHalo)→provs→zones→borders→routes→coastline→ice→goods→markets→prec→population→emblems→icons→labels→armies→markers→fogging→ruler→debug），图层显隐即编辑「层」的 UI 模型，.map 存档直接序列化整棵 SVG。缩放走 d3.zoom + 自写 RAF 合并器（pending flags OR 累积、一帧只 paint 一次），并配「语义缩放」invokeActiveZooming()：标签字号 (desired+desired/scale)/2、scale*size<6或>60 隐藏、海岸线滤镜按 zoom 三档切换（<1.5 dropShadow / 1.5-2.6 无 / >2.6 blur）、国界 halo 宽度 desired/scale^0.8、marker 尺寸 size/5+24/scale、标尺文字 10/scale^0.3——一套完整的 SVG LOD 配方。导出管线（getMapURL）是精华：克隆 SVG→getComputedStyle 与空 <g> 默认值做 diff 只内联非默认样式→循环删除空/隐藏 <g> 直到不动点→按引用裁剪 filter/pattern/symbol→图片转 base64、字体转 data-URI @font-face→href 补 xlink: 兼容 SVG1.1，产出完全自包含的单文件 SVG。架构文档明确的性能纪律值得抄：图层字符串一次性 innerHTML 注入而非逐节点 append、d3 只用于几何/标尺/投影不用于建 DOM、隐藏图层清空而非 display:none、路径坐标 rn() 取整缩短字符串、defs+use 复用字形。renderers/ 目录是 eager 自注册全局函数（window.drawX），与懒加载的 controllers 形成对照。

### 功能明细

#### 应用壳与完整生成管线调用顺序（generate() pipeline）

**算法**：generate(options) 完整顺序（public/main.js:651）：1) invokeActiveZooming；2) setSeed——无预置 seed 时从 URL ?seed= 或 generateSeed()，然后 Math.random = aleaPRNG(seed) 全局猴补丁保证全程确定性；3) applyGraphSize + randomizeOptions；4) shouldRegenerateGrid ? generateGrid() : 仅 delete grid.cells.h（网格可复用，只重掷高度）；5) grid.cells.h = await HeightmapGenerator.generate(grid)；6) pack={} 重置；7) Features.markupGrid()（洪泛标记 ocean/island/lake 特征）；8) addLakesInDeepDepressions；9) openNearSeaLakes；10) OceanLayers；11) defineMapSize（模板→尺寸/纬度表）；12) calculateMapCoordinates；13) calculateTemperatures；14) generatePrecipitation；15) reGraph()（重打包 Voronoi：pack 网格）；16) Features.markupPack；17) createDefaultRuler；18) Rivers.generate；19) Biomes.define；20) Features.defineGroups；21) Ice.generate；22) Goods.generate；23) rankCells（宜居度评分）；24) Cultures.generate + Cultures.expand；25) Burgs.generate；26) States.generate；27) Routes.generate；28) Religions.generate；29) Burgs.specify；30) States.collectStatistics + defineStateForms；31) Provinces.generate + getPoles；32) Rivers.specify + Lakes.defineNames；33) Markets.generate + Production.produce + States.collectTaxes；34) Military.generate + Markers.generate + Zones.generate；35) drawScaleBar + Names.getMapName；36) showStatistics——mapId=Date.now()，dispatch CustomEvent('map:generated',{seed,mapId}) 供测试自动化。异常时弹对话框给三选项：Cleanup data / Regenerate / Ignore。regenerateMap 为 debounce 250ms，>10000 cells 时显示 loading 幕。

**数据模型**：grid（抖动方格 Voronoi 原始图）与 pack（陆地加密重打包图）双图结构；seed 为字符串；mapHistory 数组记录每代 {seed,width,height,template,created}。

**编辑 UX**：生成与绘制分离：generate() 只产数据，drawLayers() 按图层预设渲染；错误恢复 UX（Cleanup/Regenerate/Ignore）值得抄。

**渲染**：生成完不自动画所有层，由 layers preset 决定；3D 视图开启时额外 View3d.redraw()。

**取舍**：**adapt** — 这个顺序就是 civ 层的权威依赖 DAG：宜居度→文化→聚落→国家→路网→宗教→省份→市场→生产→税收→军事→标记→事件区。WorldEngine civ.py M7+ 的节点图应按此拓扑建边（我们已有前半段物理管线）。二阶段 specify 模式（先 generate 骨架、全域数据齐后再 specify 细化命名/属性）是好模式，映射到 DAG 就是把「命名/风味」做成独立下游节点，重跑便宜。map:generated 事件 + mapId 时间戳的测试钩子直接抄给 FastAPI/前端。

#### 深洼地成湖与近海湖泊决口（addLakesInDeepDepressions / openNearSeaLakes）

**算法**：成湖：对每个 h≥20 且低于所有邻居的内陆格，从它 BFS，只走 h<threshold（threshold=h[i]+elevationLimit，elevationLimit 是用户滑杆，=80 时整个跳过）；若途中遇到 h<20 水格则可倾泻、不成湖；否则该格与同高邻格 h 置 19、t=-1，push 新 lake feature。决口：常数 LIMIT=22——遍历湖格的海岸邻格 c，若 t[c]==1 且 h[c]≤22 且 c 的某邻居属 ocean feature，则把 c 降为 h=19 水道，整湖 feature 并入 ocean（注释引 Ancylus 湖原型）。Atoll 模板跳过。

**数据模型**：grid.cells.h/t/f + grid.features[{i,land,border,type}]。

**编辑 UX**：lakeElevationLimit 是 Options 里的作者滑杆：一个数字控制「世界有多少内流湖」，且 80=关闭。

**取舍**：**already_better** — WorldEngine 的多重网格侵蚀+洼地填充在物理上已远强于此。但值得偷的是作者控制面：把「内流湖倾向」和「近海湖决口阈值」暴露成 DAG 节点参数（我们目前是物理涌现、作者不可调），作为叙事需求（『这里要有个大内海』）的快速干预路径。

#### 温度模型（calculateTemperatures）

**算法**：三段线性纬度模型：热带带 [16°N,20°S] 内 T=T_eq−|lat|×0.15；北段梯度 (T_北回归线−T_北极)/(90−16)，南段同理（南北极温度独立可设，默认 -30/-15，赤道 27）。海拔递减：h≥20 时 drop=((h−18)^heightExponent)/1000×6.5 ℃/km。逐行计算（利用方格网格行同纬度），结果 clamp 到 Int8 [-128,127]。

**数据模型**：grid.cells.temp Int8Array；options.temperatureEquator/NorthPole/SouthPole 三个用户参数。

**编辑 UX**：World Configurator 对话框实时改三个温度参数+风向重算气候。

**取舍**：**already_better** — 我们有真温度场。可偷两点：a) 南北极温度不对称作为一等公民作者参数（星球倾角/大陆分布叙事）；b) 6.5℃/km 干绝热率与我们一致，可当交叉验证常数。

#### 降水模型（generatePrecipitation）——1D 风带行进

**算法**：纬度带降水系数表 latitudeModifier=[4,2,2,2,1,1,2,2,2,2,3,3,2,2,1,1,1,0.5]（每 5° 一档，编码 Hadley/Ferrel/极地环流：0-5° x4 湿、20-30° x1 干、50-60° x3 湿、85-90° x0.5）；风向按 30° 六层 tier 取 options.winds[tier]（默认 [225,45,225,315,135,315]，用户可改），角度判 isWest/isEast/isNorth/isSouth。passWind：从上风边缘每行携带 humidity=maxPrec−h[first]（maxPrec=120×modifier×latMod，纵向风 60×，modifier=(cells/10000)^0.25×precInput/100），逐格行进：temp<-5 permafrost 不通量；水格上方吸湿 +5×modifier（clamp maxPrec）、登陆格得海岸降水 humidity/rand(10,20)；陆格降水 normalLoss=max(humidity/(10×modifier),1) + Δh×(h_next/70)²（地形雨），降水>1.5 回蒸发 1；h>85 山脉一次性倒空湿度（雨影）。最后画风向箭头符号（⇉⇇⇊⇈）。

**数据模型**：grid.cells.prec Uint8Array；依赖方格网格行/列结构（current+next 索引步进），这是 FMG 少数强依赖规则网格的算法。

**编辑 UX**：World Configurator 里六个 30° 风带各自一个角度转盘——作者直接摆布行星风系。

**取舍**：**already_better** — 我们的 cell-graph 湿度平流已是 2D 且不依赖行结构。值得偷：a) 六层风带角度作为作者可调参数注入我们的平流方向场（现在是模拟出的，加『作者覆写风带』节点）；b) 那张 18 档纬度降水系数表可做我们湿度场的 sanity-check 先验；c) (cells/10000)^0.25 的分辨率归一化思路——任何逐格累积量都要做网格密度补偿，我们跨 freq 的 CellField 重采样已有此意识。

#### reGraph 重打包（pack 网格生成）

**算法**：从 grid 生成 pack：剔除所有深海点（h<20 且 t 不是 -1/-2）；t==-2（湖）每 4 个丢 3 个、湖面点全丢；对海岸线格（t==1/-1）在与同类型邻格连线中点插入加密点（距离²≥spacing² 才插，避免过密）；用新点集重算 Voronoi 得 pack.cells/vertices；h 继承、g 记录 pack→grid 父格映射；area=|d3.polygonArea| clamp Uint16。效果：陆地/海岸高分辨率、深海零成本。

**数据模型**：pack.cells.p/g/h/area + createTypedArray 按 maxValue 自动选 Uint8/16/32。

**取舍**：**skip** — 球面测地网格分辨率均匀且我们靠放大细节层做局部精度，不需要重打包。但「深海不配拥有 cell」的预算思想在 N=2048（4200 万 cell）尺度值得记住：civ 层查询可用海陆掩码索引只遍历陆地子集（civ.py 的 scipy 图已可这样做）。

#### 宜居度评分 rankCells（聚落选址前置）

**算法**：s[i] = biome habitability（0 则不可居直接跳过）+ normalize(fl+conf, meanFlux, maxFlux)×250（大河与汇流点重赏）− (h−50)/5（低地加分）+ 海岸表：estuary(有河的海岸)+15、ocean_coast+5、save_harbor(harbor==1 单一水邻)+20、freshwater 湖+30、salt+10、frozen+1、dry/sinkhole−5、lava−30；再 /5 得基础分；资源加成：本格有 good 则 value+10，邻格 good 取均值直加。人口 pop[i] = s>0 ? s×area/meanArea : 0（按格面积归一）。meanFlux 用中位数、maxFlux 用 max(fl)+max(conf) 归一。

**数据模型**：pack.cells.s Int16、pop Float32；scoreMap 常数表。

**取舍**：**adopt** — 直接移植到 civ.py 聚落选址评分：我们已有真实河流 Q（Leopold 宽度）、湖泊类型、生物群系 habitability，公式逐项可映射且我们的输入质量更高。淡水湖 +30 > 安全港 +20 > 河口 +15 的相对权重是经数年用户校准的好起点；『邻格资源均值』的一圈卷积思路也照搬。

#### MFCG 城市生成器互操作（跨工具 URL 契约）

**算法**：双向：a) 出站——burg 预览链接带 seed（地图 seed+burg id 组成 13 位）与 population/coast/port/river/citadel/plaza/shanty/temple/walls 参数跳 watabou MFCG；b) 回站——?from=MFCG 时 findBurgForMFCG：按 coast/port/river 三谓词过滤 burg，逐级放宽（先全匹配→翻转 port/river→再放宽），d3.scan 选 |population−size| 最小者；然后把 document.referrer 的 searchParams 逐个写回 burg 对象（name/size/seed→MFCG/shantytown/其余全数值化直塞），zoomTo 该 burg 并 15 秒提示。

**数据模型**：burg.MFCG(seed)/burg.link(自定义链接)/citadel/plaza/shanty/temple/walls 五个 0/1 城市特征位——专为城市生成器准备的接口字段。

**编辑 UX**：burg 编辑器一键「在 MFCG 打开城市图」，seed 稳定所以同一 burg 永远生成同一座城。

**取舍**：**adapt** — M15 城市生成的接口设计范本：worldstore 聚落实体应现在就预留 city-gen 契约字段（种子=世界seed+实体id 保证稳定、citadel/walls 等布尔特征、人口、临水类型），使 tensor-field 街道生成器成为可独立调用的下游服务。『referrer 参数回写实体』提醒我们 geoquery/编辑 API 要支持外部工具回写用户层字段。

#### 缩放系统：RAF 合并 + 语义缩放 invokeActiveZooming

**算法**：zoomRaf：d3.zoom 事件里只更新 scale/viewX/viewY 与 OR 累积的 pendingScaleChange/pendingPositionChange 两旗标，rafId 存在即返回；RAF 回调消费旗标：位移→重画坐标网格，缩放→invokeActiveZooming+比例尺重画，两者→minimap 更新。语义缩放常数：标签字号 rel=max((d+d/scale)/2,1)，rel×scale<6 或 >60 隐藏；纹章 size×scale<25 或 >300 隐藏（且首次可见才惰性渲染 COA）；海岸滤镜 scale≤1.5 dropShadow / ≤2.6 无 / >2.6 blur；国界光晕 width/scale^0.8，<0.1 整层隐藏；marker 尺寸 size/5+24/scale；标尺文字 10/scale^0.3×2。zoom 域 [1,20]。

**渲染**：全部靠改 attribute/class，不重建 DOM；hideLabels/rescaleLabels 是用户开关。

**取舍**：**adapt** — 球面 Three.js 将来叠矢量层（河/边界/聚落标签）时的 LOD 配方：按相机距离替代 scale，直接套『目标字号与屏幕字号折中 (d+d/s)/2』和可见窗口 [6px,60px] 这两条经验规则；halo/线宽的 s^0.8 亚线性缩放让边界在远景不糊成一坨。RAF 旗标合并模式抄给 React 工作台的相机→overlay 同步。

#### .map 存档格式（save.ts prepareMapData）

**算法**：单文件 46 段、以 \r\n join 的文本格式：段0 params=版本|license|日期|seed|宽|高|mapId（管道分隔）；段1 settings 27 个管道字段（含 JSON.stringify(options) 嵌在第19字段，废弃字段保留空串占位保持索引兼容）；段2 坐标 JSON；段3 生物群系 color|habitability|name 三个 CSV；段4 notes；段5 整棵序列化 SVG（先克隆、复位 transform、清 ruler 与贸易动画）；段6 grid 概要 JSON（含 points 原始坐标——Voronoi 可重算故不存拓扑）；段7-11 grid 典型数组 CSV（h/prec/f/t/temp）；段12-15 pack.features/cultures/states/burgs JSON；段16-27 pack 逐格数组 CSV（biome/burg/conf/culture/fl/pop(圆整4位)/r/废弃road/s/state/religion/province）；段28 废弃；段29-44 religions/provinces/nameBases(与默认 diff、相同则省 b 串)/rivers/rulers/fonts/markers/cellRoutes/routes/zones/ice/good/goods/markets/deals/cells.market；段45 自定义商品图标 outerHTML（换行折叠成空格防 CRLF 切分）。三个保存目标：indexedDB(ldb.set('lastMap'))、下载 .map、Dropbox。customization≠0 时拒存。

**数据模型**：关键设计：Voronoi 拓扑不入档（由 points 重算）；SVG 即样式载体；typed array 存 CSV 文本。

**取舍**：**skip** — 我们的 worldstore+内容寻址缓存+rhombus-atlas 二进制在规模上碾压此格式（4200 万 cell 不可能 CSV 化）。但『单文件项目导出』的用户价值要补：WorldEngine 应有 .weworld 打包导出（manifest+atlas 分块+实体 JSON+zstd），供备份/分享；『派生数据不入档、加载时重算』原则与我们 DAG 缓存哲学一致。

#### 加载管线与格式嗅探（load.ts uploadMap/parseLoadedResult）

**算法**：FileReader 读 ArrayBuffer→TextDecoder；前 10 字符含 '|' 判内部格式，否则按 base64 decodeURIComponent(atob) 解旧格式；正则抓 <svg id="map"> 段修 CRLF→LF（防 SVG 内换行破坏 \r\n 分段协议）；全文按 \r\n split 成段数组；解析失败则走 gzip 分支：Blob.stream().pipeThrough(DecompressionStream('gzip')) 解压后递归重试（.gz 存档支持零依赖）。版本门：<0.70.0 ancient 拒载、>当前 newer 拒载、旧版 auto-update、损坏（段<10 或无段5）invalid。另有 loadMapFromURL：?maplink= 直接 fetch CORS 加载（120s AbortController 超时），配合 Dropbox 共享链实现『URL 即地图』分享。quickLoad 从 indexedDB，5 分钟内工作免确认直接载。

**数据模型**：版本号取自段0第一个管道字段。

**编辑 UX**：拖放 .map/.gz 到窗口任意位置即加载（dragover 全屏遮罩提示）。

**取舍**：**adapt** — 抄三点到 FastAPI/前端：a) maplink 模式——世界包 URL 参数直载，配 R2/S3 签名链接实现零后端分享；b) 多格式嗅探+解压递归重试的宽容加载器；c) 近期工作免确认（<5min）的快速恢复启发式。

#### 版本自动迁移（auto-update.ts resolveVersionConflicts）

**算法**：42 个 isOlderThan(tag) 顺序门（1.0.0→1.132.0），每门就地修 pack/SVG/options：典型操作有 (1) 补跑缺失子系统——pre-1.0 补 Religions.generate/Provinces/Zones，pre-1.124 补整个经济层（Goods/Markets/Production/collectTaxes + 按政体生成税率）；(2) SVG→数据模型迁移——1.7 从 <use> 元素反解 markers（parseTransform 拆位移、从 defs symbol 读 icon/px/dx/dy/fill/stroke、findCell(x,y) 反查 cell）、1.100 从 <g dataset> 反解 zones、1.111 从 polygon.points 反解 ice；(3) 字段重构——1.86 origin→origins[]、1.91 'custom' 字符串→{custom:true}、coaSize→coa.size；(4) 数据修复——1.113 zone.cells 去重、1.88 删坏 shield。迁移在 load 尾部动态 import（代码不进主包）。旧版结果元素属性够不到时用启发式重建（1.21 用 getPointAtLength 从 path 反推河流 source/mouth）。

**数据模型**：迁移单向、就地、无回滚；ancient(<0.70) 直接放弃并指去归档版本。

**取舍**：**adopt** — worldstore/存档演进的标准答案：顺序版本门 + 迁移函数惰性加载 + 『补跑生成器』策略（新增模拟层时老世界加载即自动获得该层，我们的 DAG 天然支持——新节点对旧世界就是 cache miss 重算）。给 worldstore 实体 schema 加 version 字段并现在就建 migrations/ 目录，别等格式破裂。教训：FMG 早期把数据存在 SVG DOM 里，付出了十几个『从 SVG 考古反解数据』的迁移门——渲染载体永远不要当数据库。

#### 加载期数据完整性自修复（load.ts data integrity 段）

**算法**：载入后全量引用体检：cells.state/province/culture/religion/burg/r 中引用不存在或 removed 实体的格子批量归 0 并 console.error；burg 检查：capital 布尔转数字、burg0/removed 带 lock 则剥锁、缺 cell/坐标则标 removed、port<0 归 0、cell 越界用 findCell(x,y) 重定位并重建 cells.burg 反向映射、state 无效归中立；state 检查：中立区 burg 标了 capital 则剥夺、一国多都保留第一个其余降格、有城无都则第一 burg 升都（都伴随 Burgs.changeGroup 重分组）；province 挂在 removed state 上则整省 removed；route<2 点删除；cells.routes 邻接表清空对象与悬空 routeId；marker id 重复则重编号（连带改 DOM id 与 notes id）后按 i 排序。另有『striping 检测』：cells 数组长度不一致或 feature 顶点悬空则直接抛错建议 ERASE 模式修复。

**取舍**：**adopt** — worldstore 加载/纪元切换时跑同款体检：实体引用（聚落→政体→文化→cell）悬空自动降级到中立/无 + 结构化日志，而不是崩。『一国必有且仅有一都』这类领域不变量修复清单直接搬进 civ 层校验器；配合我们的锁语义还要加『removed 实体不得持锁』。

#### Dropbox 云存储与共享链接（cloud.ts）

**算法**：Provider 接口 {auth,save,load,list,getLink,initialize}；OAuth：window.open('./dropbox.html') 弹窗授权，BroadcastChannel('dropbox-auth') 回传 token（120s watchdog），access/refresh/expiry 三 token 存 localStorage，SDK 带 refreshToken 自动续签；call() 包装器捕获 DropboxResponseError 自动重新 auth 重试一次。共享：sharingListSharedLinks 有则复用、无则 sharingCreateSharedLinkWithSettings(public,viewer)，把 www.dropbox.com 替换成 dl.dropboxusercontent.com（CORS 友好直链）再拼 ?maplink= 成可分享 URL。CloudStorage 门面把 provider 对象拍平成纯方法集以适配注册表的 dispatch-only 契约。

**取舍**：**skip** — WorldEngine 有自己的服务端持久层，不需要浏览器端云盘。可分享 URL 的产品概念已在 maplink 条目覆盖。

#### SVG 导出管线（export.ts getMapURL + inlineStyle + removeUnusedElements）

**算法**：克隆 #map→按选项删 debug/labels/water/ice/vignette/scaleBar，fullMap 时复位 transform 并重画比例尺；样式内联：创建空 <g> 取 getComputedStyle 作基线，遍历所有 g/#ruler*/#scaleBar>text，仅写与基线不同且无同名 attribute 的属性（跳 cursor）——最小化样式串；瘦身循环：反复删除无子/display:none/.hidden 的 <g> 直到不动点；按实际引用裁剪 filter（查 [filter='url(#id)']）/pattern（查 fill 引用）/symbol（查 use href）；资源自包含：oceanPattern/texture/marker 图片/军旗图片全部 getBase64 内嵌，用到的字体 loadFontsAsDataURI 生成 @font-face 塞进 defs<style>；defs 按需回填 relief 图标/罗盘/burg 图标/商品图标/锚/网格 pattern/hatching；SVG1.1 兼容：href→xlink:href；armies 文字样式补内联 <style>。产出 ObjectURL（5s 后 revoke）。

**渲染**：这是『屏幕 SVG≠导出 SVG』的完整解耦：屏幕靠 CSS 级联，导出靠 diff 后的内联。

**取舍**：**adapt** — WorldEngine equirect 导出与未来 2D 制图导出的参考实现：computed-style-diff 内联法与『按引用裁剪 defs』直接可移植（我们导出 SVG 地图给出版/小说插图是明确需求）。字体转 data-URI 对中文字体需子集化（unicodeRange 字段 FMG 已有雏形）。

#### 位图导出：PNG/JPEG/PNG 瓦片（export.ts）

**算法**：PNG：getMapURL('png')→Image→canvas(svgWidth×resolution)→toBlob 下载，resolution 用户滑杆。JPEG：同路径，quality=min(rn(1−resolution/20,2),0.92)——分辨率越高压得越狠控制文件体积。瓦片：先渲染带 debug 网格的 schema.png 总览；再按 tilesX×tilesY 切图（tileW=(W/tilesX)|0），行号字母编码 A..Z,AA..（getRowLabel），每片 drawImage(源裁剪→scale 放大) 逐片 toBlob 塞 JSZip（懒加载 script），产出 zip：schema.png + A1.png..；面向 VTT（桌游虚拟桌）打印/分屏场景。

**取舍**：**skip** — 我们走 MapLibre MVT（M15）与服务端渲染，浏览器 canvas 切瓦片不适用。JPEG 质量随分辨率反比这个小公式可留给导出服务。

#### GeoJSON 导出五件套（export.ts saveGeoJson*）

**算法**：坐标统一经 getCoordinates(x,y,mapCoordinates) 投影到 [lon,lat] 圆整 4 位。Cells：每 cell 一个 Polygon（顶点链闭环），properties 含 id/height(单位换算后)/biome/type/population(rural+urban)/state/province/culture/religion/neighbors——邻接表也导出！Routes：LineString+group/name。Rivers：导出时重跑 Rivers.addMeandering(cells,points) 再投影——蜿蜒是渲染期数据不入库，导出现算。Markers：Point+合并对应 note 全文。Zones：最讲究——connectVertices 沿『zone 内/外』谓词追踪边界顶点链，多连通分量各成环（≥4 点才有效 LinearRing），单环 Polygon/多环 MultiPolygon，完全被 zone 包住的湖（shoreline 全在 zone 内）识别为洞并跳过。

**数据模型**：cell 集合→边界多边形的通用算法是 connectVertices（顶点双向环追踪），全 FMG 复用（州界/省界/zone/文化区）。

**取舍**：**adopt** — geoquery 应加 GeoJSON 输出端点（写作工具链/GIS 互操作，lon/lat 我们是球面原生零投影损失）。cell 集→边界环追踪算法要移植到测地网格（goldberg 网格顶点度=3 与 Voronoi 相同，算法几乎原样可用），供政体边界/文化区/『事件区』矢量化——这正是我们缺的球上矢量叠加层的数据侧。『蜿蜒不入库、导出重算』与我们河流细化即 DAG 节点的做法一致。

#### JSON 数据导出四档（export-json.ts）

**算法**：Full/Minimal/PackCells/GridCells 四档：Full=info+settings+坐标+pack 全量+grid 全量+biomes+notes+nameBases；Minimal=只留实体表（features/cultures/burgs/states/provinces/religions/rivers/goods/markers/markets/deals/routes/zones）不含逐格数组；PackCells/GridCells=逐格数据。关键转换：内部 SoA（平行 typed array）在导出时转 AoS（每 cell 一个对象 {i,v,c,p,g,h,area,f,t,haven,harbor,fl,r,conf,biome,s,pop,culture,burg,routes,state,religion,province}），顶点表同样对象化——牺牲体积换第三方可读性。编辑模式禁止导出。

**取舍**：**adapt** — 给 worldstore/geoquery 设计『档位』导出：Minimal（实体层，喂小说一致性检查足够）/Region（局部 cell 窗口 AoS）/Full（atlas 二进制）。全星球 AoS JSON 在我们尺度不可行，但 viewshed/route 查询结果的局部 AoS 导出正合适。

#### 自动保存与保存提醒（autosave.ts）

**算法**：setInterval 30s 检查：距上次保存 ≥ autosaveInterval 分钟（0=关闭）且非编辑模式，则 prepareMapData→saveToStorage(indexedDB)，tip 通知三态（saving/saved/failed）。另一条 15 分钟保存唠叨循环：8 条文案随机轮换（『别忘了存到桌面』），CTRL+Q 切换、localStorage.noReminder 永久关闭；编辑模式跳过唠叨。

**编辑 UX**：自动存只进浏览器存储，下载文件仍靠用户手动——「云端自动、导出手动」的分层。

**取舍**：**adopt** — React 工作台直接抄：worldstore 用户层编辑 30s 节流自动提交 + 手动『导出世界包』分离。我们服务端持久所以不需要唠叨循环，但『编辑事务中禁 autosave』的互斥要保留（对应我们的锁/纪元语义）。

#### PWA 安装与离线（installation.ts + sw.js 注册）

**算法**：生产域名下注册 service worker；beforeinstallprompt 被 preventDefault 后暂存 event，右上角注入『Install』发光按钮，点击弹说明对话框（按钮栏动态插入『do not ask again』复选框），确认后 deferredPrompt.prompt()；appinstalled 事件后清理；localStorage.installationDontAsk 永久静默。

**取舍**：**skip** — WorldEngine 是 C++/FastAPI 本地服务架构，PWA 离线壳不适用。

#### 新手引导（ui-tour.ts，driver.js）

**算法**：driver.js 驱动 23 步聚光灯导览：地图导航→tooltip→Options 面板→Layers 页（预设/单层开关）→Style 页（预设/元素级样式）→Options 页→World Configurator（步骤内真的调 editWorld() 打开真实对话框）→Tools 页→高度图编辑器（真实切换 customizationMenu 面板）→About→Export 面板→Save/Load。特点：onHighlightStarted 驱动真实 UI 状态（点 tab、开对话框），free-roam 步骤加 body class 允许用户实操地图，方向键/Esc 键盘导航（可编辑元素聚焦时豁免），onDestroyStarted 统一收拾（关面板、关对话框、解绑键盘）。入口按钮最多主动出现 3 次（localStorage 计数），点过即封顶。

**取舍**：**adopt** — 工作台 DAG 节点图+球视图上手成本高，driver.js（MIT、无依赖）直接引入 React 端；『导览步骤驱动真实 UI 而非截图』和『最多打扰 3 次』两条产品规则照抄。

#### 懒加载模块注册表（registry.ts + Controllers/Services 双桶）

**算法**：createRegistry(loaders)：双层 Proxy——外层按模块名返回缓存的 entry proxy，内层把任意方法名变成 (…args)=>load(name).then(m=>m[method](…args))；load 记忆化 dynamic import（每模块只取一次 chunk），trackLoad 计数器管理全局『Loading…』提示；内层 proxy 对 'then' 和 symbol 键返回 undefined，防止 await Registry.X 被误当 thenable。契约：dispatch-only（只能调方法不能读属性），每模块单导出对象，数据型模块需门面包装（CloudStorage 拍平 dropbox）。TS 侧 AsyncMethods<T> 映射类型把所有方法签名自动 Promise 化，调用点全类型安全。Controllers 51 项全懒，Services 7 项（Cloud/ExportJson/ExportMap/Installation/Load/Save/UiTour）；window.Controllers/Services 供遗留 JS。eager(value) 包装器让急加载模块伪装成懒的，切换零调用点改动。

**取舍**：**skip** — React+Vite 的 lazy()/动态 import 已覆盖需求，Proxy 魔法是为『无打包遗留 JS 与 TS 共存』的迁移期发明的，我们没有这个历史包袱。但『方法全 Promise 化的映射类型』技巧和『loading 计数器归零才清提示』可借。

#### 编辑器全清单（controllers/index.ts，51 项完整性兜底）

**算法**：注册表全量（用途一句话）：HeightmapEditor 高度图笔刷/模板/图像转高度雕刻；HeightmapSelection 预制高度图选择；CoastlineEditor+CoastlineVertexEditor 海岸线整体/逐顶点编辑；LakesEditor 湖属性；RiverCreator/RiverEditor/RiversOverview 手绘河/单河控制点编辑/河流总表；ReliefEditor 地形图标笔刷；BiomesEditor 生物群系笔刷+习性参数表；IceEditor 冰川冰山拖放；CulturesEditor 文化区笔刷+层级树；ReligionsEditor 宗教区笔刷+树；StatesEditor 国家（改色/改名/领土笔刷/regenerate 锁定语义）；ProvincesEditor 省；DiplomacyEditor 外交关系矩阵；EmblemsEditor 纹章（Armoria 模型）；BurgEditor 单聚落（人口/特征位/MFCG 链接）；BurgGroupEditor 聚落分组图标样式；BurgsOverview 聚落总表（批量改名/删/锁）；LabelsEditor 标签；NotesEditor 图例笔记富文本；MarkersEditor+MarkersOverview 标记点；RouteCreator/RouteEditor/RouteGroupsEditor/RoutesOverview 路网四件套；ZonesEditor 事件区（受灾/瘟疫等 cell 集合）；MilitaryOverview+RegimentEditor+RegimentsOverview 军事三件套；BattleScreen 战斗模拟界面；UnitsEditor 单位/度量衡定义；NamesbaseEditor 命名语料库编辑；HierarchyTree 文化/宗教演化树可视化组件；ElevationProfile 沿线高程剖面图；ChartsOverview 统计图表；Minimap 小地图；View3d 3D 视图（three.js，globe/scene 两模式+侵蚀细化烘焙）；经济系（v1.124）：GoodsEditor/GoodEditor 商品目录与单品、DistributionEditor 原料分布规则可视化构建器、ProductionChains 配方图、ProductionOverview/MarketOverview/MarketsOverview/MarketDealsOverview/ComparePrices/TradeDetails/TradeAnimationEditor 市场贸易全家桶。

**编辑 UX**：命名规律即交互规律：*Editor=可变单实体对话框、*Overview=只读全量表格（带批量操作）、*Creator=点击地图的放置模式、*GroupEditor=分组样式批量。

**取舍**：**adapt** — WorldEngine 编辑面完整性对照表：我们缺席最严重的是（按小说套件价值排序）NotesEditor 等价物（worldstore 实体已可挂文本，缺 UI）、ZonesEditor（『事件区』= news 传播/灾害叙事的地理载体，civ 层应加 zone 实体）、ElevationProfile（travel-time 查询的天然可视化）、DiplomacyEditor（政体关系矩阵喂一致性检查）、HierarchyTree（文化/语言演化树）。Editor/Overview/Creator 三态命名法直接采纳为工作台组件规范。

#### 应用启动路由与会话恢复（checkLoadParameters/generateMapOnLoad）

**算法**：启动决策链：?maplink=（正则校验 URL）→ 1s 后 loadMapFromURL；?seed= → 按 seed 生成；onloadBehavior 设为 lastSaved → indexedDB 取 lastMap 恢复；兜底随机生成。生成路径：applyStyleOnLoad→generate→applyLayersPreset→drawLayers→fitMapToScreen→focusOn(?scale/cell/burg 参数定位缩放 zoomTo 1600ms)→toggleAssistant。无 hostname（file://）直接拒跑并指引开本地服务器。

**取舍**：**adapt** — 工作台深链协议照此设计：/world/{id}?focus=cell:123&zoom=8 直达球面某格/某聚落——小说写作时从手稿引用跳回地图的关键路径（geoquery describe 的反向链接）。『上次会话自动恢复』对接 worldstore 最近纪元。

#### 架构文档与迁移策略（docs/architecture 五篇）

**算法**：目标架构四层：settings→GENERATORS→WORLD(state)→{EDITORS, RENDERERS}；铁律：生成器确定性有种子无 DOM、渲染器幂等只读无业务、控制器薄且 build-on-open/destroy-on-close、IO 序列化对称即契约。性能纪律成文：SoA typed array、派生数据不存、字符串一次注入建层、隐藏层清空、坐标取整。配置两域：map config 随 .map 走 vs app preference 只进 localStorage，永不混装；所有生成参数进单一扁平 config 供通用编辑器（无 basic/advanced 结构分裂）。future_data_model.md 规划 .map v2 为纯 JSON（meta/settings/style/data.topology/geography/civilizations/settlements/annotations 分域）。migration_guide.md 记录 d3 v5/v7 共存的经典坑：v5 selection 上 .call(v7 drag) 会以 datum-first 约定分发导致拖拽静默失效。docs/updates 仅存两篇：v1.123.0『Eroded Terrain and Satellite Texture』（3D 视图 GPU 烘焙高分辨率侵蚀网格+卫星风格纹理，view-only 不改数据）、v1.124.0『Economy』（商品/市场/贸易/税收全套）；package.json 当前 1.135.2。

**取舍**：**adapt** — 三条直接入我们的规范：a) 『map config vs app preference』分域——worldstore 世界参数 vs 工作台 UI 偏好目前有混装风险；b) 『所有生成参数单一扁平命名空间+自描述字段』正是我们 DAG 节点参数面板的正确形态；c) 『派生数据不序列化』与内容寻址缓存互为印证。v1.123 的『view-only GPU 侵蚀烘焙』印证我们 IGM 放大细节层走对了——但我们是数据真实而非视觉贴皮，already_better。

### 值得偷的技巧

- 整棵渲染 SVG 被原样序列化进 .map 存档当『样式与图层状态载体』，加载时 innerHTML 重建再重新抓取 40 个图层引用；早期版本甚至把 markers/ice/zones 的数据本体存在 SVG 属性里，导致 auto-update.ts 里十几个『从 SVG DOM 考古反解数据』的迁移门——渲染载体当数据库的反面教材，教训已被他们自己写进架构文档。
- 全局 Math.random = aleaPRNG(seed) 猴补丁实现全应用确定性——一行换来所有子系统免传 RNG，代价是任何第三方库调 Math.random 都会消耗序列破坏重放；WorldEngine 的 DAG 每节点独立 seed 派生是正确解，但要警惕 civ.py 里 scipy/numpy 的全局随机状态。
- load.ts 里被迫用『全局 d3 v5 重新 select』并写长注释解释 #1508 bug：v7 selection 派发事件不设 v5 的 d3.event 全局，混用会让 zoom/mouse 静默失效；migration_guide 还记录了 v5 selection 上挂 v7 drag 会以 datum-first 约定调用导致拖拽无声失败——大版本共存的 API 约定漂移是真实成本。
- 导出 SVG 的样式内联不是全量 dump 而是与一个临时空 <g> 的 computedStyle 做 diff，只写非默认属性；配合『反复删空组直到不动点』和按引用裁剪 defs，同一棵 DOM 产出可以小一个量级——制图导出的工程含金量都在这些细节里。
- 检查版本门与 auto-update：门一直排到 1.132.0，package.json 已是 1.135.2，而 docs/updates 目录只有 2 篇版本笔记（v1.123.0、v1.124.0）——任务要求列最近 6 个版本主题但仓库里只存在这两篇，其余版本主题只能从 auto-update 门的注释间接考古（如 1.109 burg 分组、1.111 ice 数据化、1.124 经济）。
- GeoJSON 河流导出时现场重跑 Rivers.addMeandering 再投影——蜿蜒点从不入库，是纯渲染期派生数据；与 WorldEngine 『蜿蜒细化=DAG 节点、内容寻址缓存』的设计不谋而合，互为佐证。
- gzip 支持的实现方式是『先按明文解析，抛异常了再用浏览器原生 DecompressionStream 解压递归重试』——零依赖、零格式头检测的宽容加载，包括还兼容更古老的 base64 编码存档。
- MFCG 互操作里 document.referrer 的查询参数被逐个直接写回选中 burg 对象（b[key]=+value），一个外部网站通过 referrer 就能改地图数据——跨工具集成的信任边界几乎不存在，我们做外部工具回写 API 时要引以为戒。
- 保存唠叨系统：每 15 分钟从 8 条人格化文案里随机挑一条提醒存盘，CTRL+Q 关闭——浏览器端无服务持久化产品的求生欲设计；同时 autosave 只写 indexedDB，『真正的备份』永远引导用户手动下载。
- registry 的内层 Proxy 特意对 'then' 返回 undefined——否则 await Controllers.X 会把注册表条目当 thenable 调用一个幻影 then 方法；文档明确要求维护者保留这个守卫，是 Proxy 魔法的经典暗坑。

## watabou TownGeneratorOS

### 生成管线

#### 1. 种子点分布（Fermat 螺线，Model.buildPatches）

**算法**：生成 nPatches*8 个点：角度 a = 随机起始角 + sqrt(i)*5（5 弧度增量的向日葵螺线，非黄金角），半径 r = 10 + i*(2+rand)，i=0 时 r=0（城市几何中心）。这天然产生中心密、外围疏的密度梯度——内城 patch 小、乡村 patch 大，一个公式同时解决了『城市密度』和『城乡边界』。城市规模只由 nPatches 一个参数控制。

**常数**：Small Town=6, Large Town=10, Small City=15, Large City=24, Metropolis=40（nPatches）；实际生成 8 倍数量的点，多余的成为乡村 patch

#### 2. Voronoi patch（geom/Voronoi.hx，Bowyer-Watson 增量 Delaunay）

**算法**：自写增量 Delaunay：外包框（bbox 各向外扩 25%）两个三角形起步，逐点插入——找出外接圆包含新点的三角形集合，求其边界空腔（利用『共享边在两个三角形中方向相反』做指针相等性判断），扇形重连。Voronoi region = 每个种子点周围三角形外接圆心按极角排序成多边形。partioning() 丢弃触及外框的 region。之后做定向 Lloyd 松弛：只对前 3 个点 + 第 nPatches 个点（citadel 候选）迭代 3 次 relax——只把广场/城堡候选变圆整，外围保持随机不规则。patch 按到原点距离排序，前 nPatches 个 = 内城（withinCity），第 0 个 = plaza（若掷中），第 nPatches 个 = citadel（若掷中）。

**常数**：Lloyd 迭代 3 次，只作用于 4 个种子；plaza/citadel/walls 各以 50% 概率独立掷骰

#### 3. 顶点焊接（Model.optimizeJunctions）

**算法**：遍历内城+citadel patch，凡边长 < 8 的 Voronoi 边，把 v1 合并进 v0（取中点），并通过 patchByVertex 找到所有共享 v1 的邻接 patch 就地替换引用，再去重。目的：消除 Voronoi 常见的极短边，避免后续城墙出现挨在一起的塔楼和碎墙段。完全依赖 Point 对象共享（见 data_model）。

**常数**：焊接阈值 8（对比 MAIN_STREET=2，即约 4 个主街宽）

#### 4. 城墙（CurtainWall + Model.findCircumference）

**算法**：外轮廓提取 findCircumference：对每个 patch 的每条边 (a,b)，若没有任何其他 patch 拥有反向边 (b,a) 则为外边界边，然后从任一外边 A[i]→B[i] 按 B 在 A 中的 index 链式行走闭合成多边形——O(n²) 但纯拓扑、零浮点比较。真墙时对轮廓做顶点平滑，因子 min(1, 40/nPatches)（城越小平滑越狠），citadel 顶点（reserved）除外。城门选择：候选 = 墙上『被 ≥2 个内城 patch 共享』的顶点（保证门内有街可走）；随机选一个，删除其左右各 1-2 个邻居候选（保证门距），循环直到候选 <3。选门后若门外只有 1 个乡村 patch，则沿门的外法向找该 patch 上『方向余弦最大』的顶点，用 Polygon.split 把乡村 patch 一分为二——人为制造一条出城路廊道。门顶点再做平滑。塔楼 = 墙上所有非门顶点。Citadel：给该 patch 单独建 CurtainWall，reserved = 与乡村相邻的顶点（城堡背靠城外），若 compactness < 0.75 直接 throw 重开整城。最后按 border 半径 3 倍裁掉过远的乡村 patch。

**常数**：墙平滑因子 min(1,40/nPatches)；citadel compactness 门槛 0.75；乡村保留半径 = 3×墙半径；GateWard 概率：有墙 0.5 / 无墙 0.2

#### 5. 街道拓扑（Topology + geom/Graph）

**算法**：图节点 = 全部 patch 顶点（Point→Node 双向 map），边 = patch 多边形边，权重 = 欧氏长度。blocked = citadel 轮廓 + 城墙轮廓 − 城门（返回 null 节点从而不可通行）。节点分类：内城 patch 的顶点入 inner 集，乡村顶点入 outer 集，位于 border 轮廓上的顶点两边都不进。每个城门生成一条街：gate → plaza 最近角（无 plaza 则 city center），寻路时 exclude=outer（街只走城内）；对外墙门再反向生成一条乡村路：从『gate 外法向 1000 单位处最近的节点』→ gate，exclude=inner。注意 Graph.aStar 名不副实：无启发函数、无优先队列（FIFO shift + gScore 松弛 + closed set），是个标签修正搜索，结果近似最短路。tidyUpRoads：所有街/路切成线段去重（丢弃沿 plaza 边缘的段），再链式拼接成 arteries，对 arteries 做 smoothVertexEq(f=3) 平滑。街道失败 throw → 整城重开。

**常数**：平滑权重 f=3；乡村路起点探测距离 1000

#### 6. Ward 类型分配（Model.createWards + 各 ward 的 rateLocation）

**算法**：三步：(a) plaza→Market；边境门顶点相邻的内城 patch 按概率变 GateWard。(b) 固定 36 项职业表（Craftsmen 21≈58%、Slum 5、Merchant/Market/Patriciate 各 2、Cathedral/Administration/Military/Park 各 1）做轻度洗牌（仅 len/10 次相邻交换，保持大致顺序＝重要 ward 先挑位置），逐个出队，对未分配 patch 求 rateLocation 的 argmin（无该静态方法则随机）；表耗尽后全填 Slum。判据一览——Cathedral：邻接 plaza 时 -1/面积（偏好小 patch），否则 距离×面积；Market：邻接另一 Market 判 +∞，否则 面积/plaza面积（不许比广场大太多）；Administration：邻接 plaza 得 0 否则按距离；Merchant：纯距中心距离；Slum：-距离（越远越好）；Military：邻 citadel=0，邻墙=1，否则 +∞（无墙无堡时=0 随便放）；Patriciate：每邻接一个 Park -1、每邻接一个 Slum +1。(c) 乡村：castle 门外 patch 高概率变 GateWard 郊区；其余 20% 且 compactness≥0.7 → Farm，否则空 Ward。这是教科书级的『argmin 评分函数分配』微模式。

**常数**：职业表 36 项；洗牌次数 = len/10；乡村 Farm 概率 0.2 + compactness≥0.7

#### 7. 街区几何：patch→city block（Ward.getCityBlock）

**算法**：对 patch 每条边按道路等级取内缩距离：贴墙边 MAIN_STREET/2=1.0；两端点都在某 artery 上（或与 plaza 共边）→ MAIN_STREET/2；普通内城边 REGULAR_STREET/2=0.5；城外边 ALLEY/2=0.3。凸多边形走 shrink（逐边半平面 cut，稳健），凹多边形走 buffer（逐边平移生成自交多边形→逐对边求交插点→追踪所有环取面积最大者——手写的简易 polygon offset，Clipper 出现之前的民间方案）。关键设计：内城街道从不显式画线，它是相邻 block 内缩后留出的负空间。

**常数**：MAIN_STREET=2.0, REGULAR_STREET=1.0, ALLEY=0.6（Ward 类顶部）

#### 8. 地块/建筑递归切分（Ward.createAlleys / createOrthoBuilding / Cutter）

**算法**：createAlleys（普通住区）：找最长边，Cutter.bisect 在 ratio=(1-0.8·gridChaos)/2+rand·0.8·gridChaos 处、以 ±π/12·gridChaos 的角度扰动（面积 <4·minSq 时扰动归零保持建筑矩形）垂直切开；gap 参数即切缝宽：递归外层 gap=ALLEY=0.6（留出小巷），当子块面积 ≤ minSq/(rand·rand) 时 split=false、gap=0——深层切分共享墙，联排房自然涌现。终止条件 面积 < minSq·2^(4·sizeChaos·(rand-0.5))，emptyProb 概率丢弃留空地。cut 的缝由 Polygon.peel（单边内缩）实现。createOrthoBuilding（城堡/教堂/农舍）：只沿两个正交方向 c1（最长边方向）/c2 切，每次选与当前最长边更垂直的那个，ratio 0.4~0.6，终止 面积<minBlockSq·2^(2·normal-1)——产出 L 形/十字形复合体量；外层 while(true) 重试直到非空。Cutter.ring（40% 的教堂）：每边向内 thickness 平移线依次 cut（短边优先），削出一圈『皮』留中庭——回廊/修道院。Cutter.radial/semiRadial（Park）：从质心（或最靠质心的顶点）向每条边拉三角扇区，shrink 出 ALLEY 缝隙——林地小径。Farm：4×4 矩形随机旋转，放在 随机顶点→质心 的 30%~70% 插值点上再 ortho 切分。各 ward 参数即风格：Slum minSq=10~40/gridChaos 0.6~1.0（碎而乱）、Patriciate 80~110/空置 0.2（大宅稀疏）、Military gridChaos 0.1~0.4 + emptyProb 0.25（营房+操场）。

**常数**：每 ward 的 (minSq, gridChaos, sizeChaos, emptyProb)：Craftsmen(10+80rr, .5-.7, .6, .04)、Merchant(50+60rr, .5-.8, .7, .15)、Patriciate(80+30rr, .5-.8, .8, .2)、Slum(10+30rr, .6-1.0, .8, .03)、Administration(80+30rr, .1-.4, .3)、Gate(10+50rr, .5-.8, .7)；Castle 内缩 4.0、minBlockSq=√S·4、fill .6；Cathedral ring 厚 2+4rand 或 ortho(50,.8)

#### 9. 城郊密度衰减（Ward.filterOutskirts）

**算法**：对非 enclosed（不完全被城区/墙包围）的 ward，建筑存活率随『到有人气边缘的距离』衰减：populated edges = 在 artery 上的边（权重 1）+ 与城内邻居共享的边（enclosed 邻居 1，普通 0.4）；每条记录该 patch 内到此边的最大垂距 d 作归一化分母。每个顶点算 density：城门顶点=1，全城内共享顶点=2·rand，其余 0；建筑中心用 Polygon.interpolate（反距离加权坐标）对顶点 density 插值得人口权重 p，minDist/=p，存活判据 Random.fuzzy(1) > minDist。产出效果：城外建筑沿道路成带状蔓延、离路越远越稀——ribbon development 的十行实现。

**常数**：非 artery 城边权重 0.4；密度：门=1、内城顶点=2·rand

### 数据模型

整个系统的地基是『共享 Point 对象身份的平面网格』：Voronoi 顶点（三角形外接圆心）作为同一个 Point 实例被所有相邻 patch 的 Polygon 引用，因此全部拓扑查询都是指针相等而非坐标容差比较——Polygon.contains 是 indexOf、findEdge(a,b) 是 indexOf+next 判等、Model.patchByVertex/getNeighbour/borders、findCircumference 的反向边检测、Topology 的 Point↔Node 双向 Map、焊接时改一处顶点全网生效，全都靠这一条。Polygon 是 Array<Point> 的 abstract 包装（@:forward），带 square(鞋带公式，CCW 为正)/centroid/compactness(4πA/P²)/cut/split/peel/shrink/buffer/inset/smoothVertex 等一套完备的多边形手术刀。Patch{shape, ward, withinCity, withinWalls} 是唯一空间单元；Street=Polygon(折线)；CurtainWall{shape, gates:Array<Point>, towers, segments:Array<Bool>}；Ward 持有 geometry:Array<Polygon>（建筑脚印）。顶层是异常驱动的拒绝采样：Model 构造函数 do{build()}catch{}while 无限重试，任何阶段（citadel 不够圆、街连不通、门选不出）throw 即整城报废用下一个随机流重来。waterbody:Array<Patch> 字段已声明但通篇从未赋值。

### 渲染

纯 2D openfl 矢量（CityMap.hx）：z 序为 乡村路→各 patch 建筑→hover 热区→墙。路的画法是经典描边路风格：先画 宽=MAIN_STREET+0.3 的 medium 色粗线，再叠 宽=MAIN_STREET-0.3 的纸色芯线，得到带轮廓的空心路；而城内街道根本不画——它是相邻 block 内缩留下的纸色负空间，与乡村路视觉自动统一。建筑=light 填充+dark 描边（0.3），Castle 描边×2、Cathedral 常规、Park 只用 medium 填扇区。墙=1.8 粗 dark 多边形，塔=顶点处半径 1.8(castle×1.5) 实心圆，门=沿墙切向的短粗垂直刻线。Palette 只有 4 个色阶（paper/light/medium/dark），8 套预设（默认羊皮纸、BLUEPRINT、INK、NIGHT 等）——整张图信息全靠形状而非颜色编码，这是它风格耐看的核心。PatchView 挂透明热区做 hover tooltip 显示 ward 标签。缩放按 DPI 及 cityRadius 自适应。

### 局限

OS 版没有任何环境约束：无地形/DEM、无水体（waterbody 字段是死代码）、无河流海岸（自然也无桥梁码头）、无道路层级（乡村路都是直连门的单段路，不成网）、无农田肌理/树木/山影、无文字标注（仅 hover）、只有一圈墙（无多期扩城墙）、ward 表硬编码 36 项且与城市规模无关、平面欧氏几何、伪随机 LCG(48271) 全局单流、异常重试导致同 seed 不同平台可能产出不同城（重试次数吃随机流）。线上 MFCG 的河流/海岸/山丘/棚户区/农田/地名都不在此代码中。另有两处名不副实：Graph.aStar 无启发式无优先队列；CurtainWall 构造器 this.real=true 硬编码无视参数（被同名参数遮蔽而未发作的隐性 bug）。规模上限也低：Metropolis 也只有 40 个 patch，所有邻接查询都是 O(n²) 暴力。

### WorldEngine 移植笔记

尺度前提：我们 N=2048 网格 cell 边长 3.9km，而这套生成器的整城半径折合数百米——城市生成必然发生在单 cell 内部的局部切平面（gnomonic/方位投影）上，因此它的全部平面几何算法无需球面化，原样可用；球面只提供边界条件。可移植清单——【adopt】(1) Ward.rateLocation argmin 分配微模式：每种城区一个评分函数、逐个出队取最优 patch，直接换成我们的可达性/地价/坡度场做评分项，civ.py 聚落内部分区一天可接入；(2) createAlleys/createOrthoBuilding/ring/radial 四个地块→脚印生成器：与投影无关的纯平面递归切分，gap=0 涌现联排房、ring 出回廊、(minSq,gridChaos,sizeChaos,emptyProb) 四参即风格——M15 在 straight skeleton 地块内的建筑脚印层可直接采用；(3) getCityBlock 的『按邻接道路等级逐边内缩、街道=负空间』：与 M15 tensor-field 街道天然契合，MVT 渲染时街道不用单独出层；(4) 描边路+4 色阶 palette 的制图风格，MapLibre style 半小时即可复刻；(5) filterOutskirts 的带状城郊衰减：把 populated-edge 距离换成我们的 travel-time 场，直接得到沿官道的 ribbon development，且能喂 news 传播模型。【adapt】(6) 中心密外围疏的种子布点：Fermat 螺线换成从聚落种子+人口场采样，Voronoi 后照抄其定向 Lloyd（只松弛市政核心）；patch 生成时先用河流/海岸多边形裁剪，被裁 patch 归入它从未实现的 waterbody——等于替它把水做完；(7) 城墙 findCircumference/选门逻辑：外轮廓行走照搬（但把指针相等改成我们的 half-edge 索引邻接）；选门判据反转——OS 是先随机选门再造路，我们应让 civ.py 既有区域路网与墙轮廓的交点定门，再用其『切分外部 patch 造出城路廊』的手法保证门外有廊道；(8) Topology 的 patch 顶点图寻路：换成带坡度惩罚的真 A*（它的假 A* 别抄），blocked=墙−门 的机制保留，另加河流边 blocked、桥点作为额外 gate——桥即水上的城门，这个抽象很干净；(9) 顶点焊接 optimizeJunctions：任何 Voronoi 系管线都该带，阈值改为与街宽联动。【skip/replace】异常整城重试→改为阶段级带重试计数的确定性 jitter（重试次数进 cache key，否则内容寻址缓存被打破）；Polygon.buffer/shrink 手写 offset→一律 Clipper2；O(n²) 指针邻接→半边结构。总体判断：这是『半天可抄完的城市骨架管线』，patch-ward-block 三层分解与我们 M15 规划（tensor field 街道+straight skeleton 地块）完全正交互补——它管拓扑与分配，M15 管几何精化。

### 值得偷的技巧

- Graph.aStar 是假 A*：无启发函数、无优先队列（FIFO shift+gScore 松弛），实为标签修正搜索，路径可能次优——但在 40 个 patch 的图上视觉无碍，作者显然知道够用就好
- CurtainWall 构造器第 23 行 this.real = true 硬编码，完全无视传入参数——被 buildGates 用同名参数遮蔽而从未发作的隐性 bug（字段只在 buildTowers 里读，而 buildTowers 恰好只对真墙调用）
- 城内街道从不被绘制：它是 getCityBlock 逐边内缩留出的负空间，靠相邻 block 各退半条街宽自动拼出，与显式绘制的乡村路视觉无缝衔接
- 联排房是涌现的：createAlleys 递归到深层时 split 标志翻转、切缝 gap 从 ALLEY=0.6 变 0，建筑共享隔墙，无任何『联排房』专门代码
- 顶层架构是异常驱动的拒绝采样：citadel 不够圆(compactness<0.75)或街连不通就 throw，do-try-while 整城重开——用『重掷整个世界』替代任何局部修复逻辑
- Lloyd 松弛只做给 4 个种子点（前 3 个+citadel 候选）：市中心圆整、外围保持 Voronoi 野性，一个『定向松弛』同时得到秩序与有机感
- 螺线角增量是 5 弧度而非黄金角 2.4，且半径带随机抖动——作者要的不是均匀 phyllotaxis 而是可控的杂乱
- waterbody 字段声明了却全篇未赋值——线上版河流海岸功能的化石级占位符
- Polygon.buffer 手写自交解算（逐对边求交插点后追踪所有环取最大者）——Clipper 普及之前的民间 polygon offset，注释还自嘲 not very reliable
- 职业表 36 项里 Craftsmen 占 21（58%），加 5 个 Slum——硬编码的中世纪职业人口学；洗牌只做 3 次相邻交换，保证 Cathedral/Market 等稀缺 ward 大致优先选址
- 选门后若门外只有一个乡村 patch，就沿门外法向把它劈成两半人为造出路廊——门的存在会反向重塑城外的地块划分
- 建筑密度插值用 Polygon.interpolate：对 patch 顶点做反距离加权归一化坐标，相当于把『顶点人口』广义重心插值到任意建筑中心——一个被埋没在 filterOutskirts 里的通用小工具
