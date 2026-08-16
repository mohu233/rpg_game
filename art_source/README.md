# 美术源文件

此目录保存不会被游戏直接加载或打包的美术源文件和历史资料。

- `player`：角色原图、旧序列图和生成中间结果
- `legacy_bmp`：迁移前使用洋红色透明键的 BMP 备份
- `object_extras`：对象模块中未被运行时引用的备份图片
- `reference`：截图、生成参考和扫描结果
- `legacy_scenes`：旧场景目录备份；当前地图模板位于 `assets/maps`

运行时资源规范见 `docs/assets.md`。不要从此目录直接填写 `item.json` 或 `object.json` 的图片路径。
