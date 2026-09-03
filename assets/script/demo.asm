# Siglus 移植框架自检脚本（demo 文本方言）
# 坐标约定：layer 的 x = 中心 X，y = 底边 Y（y=0 表示贴画面底部）

label start
fade 1 600
bg bg_grad
wait 300

alpha 1 0
layer 1 char_a 420 720
alpha 1 255

text "少女" "你好，安卓。"
text "" "这是最小可运行的 Siglus 兼容引擎骨架：分层合成 + 转场 + 文本机 + 选择支。"
se se_click

select "继续"|"结束"
jz 0 continue

text "" "你选择了结束。"
jmp finish

:continue
text "" "你选择了继续。"
text "" "支持 ruby 注音：[漢字](かんじ)。"

:finish
text "" "（演示结束）"
end
