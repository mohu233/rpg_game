# 资源目录规范

`assets` 只存放游戏和场景编辑器运行时需要加载的文件。运行时图片统一使用带 Alpha 的 PNG。

## 目录

- `assets/characters/player`：主角站立、行走、攻击序列图
- `assets/objects/nature`：树木、石头、灌木等自然对象
- `assets/objects/buildings`：可建造设施、地板和房屋
- `assets/objects/special`：锚点、传送点、结局祭坛
- `assets/items`：按 `raw_resources`、`materials`、`gems`、`seeds`、`crops`、`spirit_stones`、`consumables`、`buildings` 等分类
- `assets/terrain`：自然地形、人工地形和过渡遮罩
- `assets/effects`：世界特效
- `assets/ui`：界面资源
- `assets/maps`：地图模板

PSD、旧 BMP、截图和生成中间文件存放在项目根目录的 `art_source`，不会被打包为运行资源。

## 加载规则

- 对象和物品加载器递归扫描分类目录中的 `object.json`、`item.json`。
- 对象图片必须是 PNG；JSON 内的对象图片路径相对于该对象模块目录。
- 物品 `icon` 和 `world_image` 路径相对于 `assets`。
- 地图仍保存对象 `type` 和物品 `id`，整理目录不会改变旧地图或存档标识。

## 更新角色

运行：

```powershell
python tools/update_player_sprites.py
```

输出默认写入 `assets/characters/player`。
