#已弃用system服务
手动调试前这样做：
sudo systemctl stop sp-vision.service

确认停了：
systemctl status sp-vision.service

然后再手动跑：
cd /home/jwj-rune/Downloads/sp_vision_25
./build/standard_mpc_fyt configs/infantry.yaml

调完以后恢复后台自启服务：
sudo systemctl start sp-vision.service