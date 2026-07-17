# 管线 DAG 设计（定稿 v1）

调研结论：无可直接复用的库（taskflow/tbb=编译期任务图；graphtik/dask=无参数化
类型；QtNodes=桌面绑定）。**借 ComfyUI 的模式不借代码**：前端图 JSON → Python
拓扑执行器（声明式节点注册表 + 内容寻址缓存）→ 原生层重算子。

## 用户拍板（2026-07-18）

1. 前端 React+TS+vite（dockview + React Flow）。
2. 节点图一期直接自由 DAG。
3. 频率不匹配：**硬校验 + UI 一键插入 resample**（不做隐式重采样）。
4. 执行器在 **Python 服务器层**（~300 行；graphlib 拓扑 + 类型校验 +
   blake2b 内容寻址缓存 + SSE 进度/取消）；C++ 只暴露算子。
5. **paint 是源节点**：画笔层=图中节点，接线决定注入位置；编辑→缓存失效→
   只重算下游锥体。

## 类型代数（边）

- `CellField(frequency)` — 测地格胞 f32 数组；频率是类型参数，边校验要求相等。
- `Raster(w,h)` — 仅 export 之后存在（equirect 降级为导出格式，IGM 化达成）。
- 参数（数值/枚举/种子）是节点属性，不上边。

## 节点注册表（一期）

| 节点 | 输入 | 输出 | 实现 |
|---|---|---|---|
| noise | — | field@F | C++ noise_cells（球面求值） |
| tectonics | — | elevation/uplift/age/crust @F_tect | C++（stage 现在同时落 cell 层） |
| resample | src@* | out@F | C++ locate 重心插值 |
| math | a@F, b@F | out@F | numpy（add/sub/mul/lerp/min/max） |
| mask | src@F | out@F | numpy（threshold/smoothstep） |
| physics | z0@F, uplift@F | elevation/temp/precip/biome/… @F | C++（z0 直吃 cell field） |
| paint | — | field@F | 存储 blob（作者画笔） |
| export | field@* | (raster 层/cell 层落盘) | C++ rasterize_cells |

缓存键 = 节点参数 hash + 上游输出内容 hash。旧 `generate(params)` 保留原行为；
新图是 IGM 主链（combine 在 cell 空间，消除双重重采样）。
