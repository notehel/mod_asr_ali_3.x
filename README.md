# FreeSWITCH ASR模块 [NlsSdkCpp3.x]

[阿里云ASR](https://help.aliyun.com/product/30413.html?spm=a2c4g.11186623.2.10.6b634c07NBBDiY)和FreeSWITCH直接对接，识别结果通过ESL输出  
阿里云语音识别SDK: [**NlsSdkCpp3.x**]

### 编译安装

1. 安装Freeswitch [**Install**](https://freeswitch.org/confluence/display/FREESWITCH/CentOS+7+and+RHEL+7)

2. 下载`mod_asr`代码
```
git clone http://gitlab.yshome.com:8081/elegant/mod_asr.git
```
3. 编译
FreeSWITCH和NlsSdkCpp3.x路径根据自己情况修改
```
make
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../lib/linux
```
4. 安装
```
cp mod_asr.so /usr/local/freeswitch/mod
#编辑modules.conf.xml添加mod_asr模块
vim /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml
<load module="mod_asr"/>
```
5. 验证
启动freeswitch查看mod_asr是否加载成功
```
freeswitch -nc -nonat
fs_cli -x "show modules"|grep asr
application,start_asr,mod_asr,/usr/local/freeswitch/mod/mod_asr.so
application,stop_asr,mod_asr,/usr/local/freeswitch/mod/mod_asr.so
```

#### 使用

1. 申请阿里云AccessKey和Secret
2. fs_cli执行

start_asr参数:
```
originate user/1001 'start_asr,echo' inline
```

3. dialplan执行
```
<extension name="asr">
    <condition field="destination_number" expression="^.*$">
        <action application="answer"/>
        <action application="start_asr" data=""/>
        <action application="echo"/>
    </condition>
</extension>
```

#### 开发
订阅`CUSTOM asr_start` `CUSTOM asr_update` `CUSTOM asr_stop` 事件
fs_cli可以通过`/event Custom asr_start  asr_update asr_stop`订阅事件
识别结果通过esl输出
```
RECV EVENT
Event-Subclass: start_asr
Event-Name: CUSTOM
Core-UUID: dbc6fb6a-16e6-44cb-8be8-a49397cc3c5f
FreeSWITCH-Hostname: telegant
FreeSWITCH-Switchname: telegant
FreeSWITCH-IPv4: 10.10.16.180
FreeSWITCH-IPv6: ::1
Event-Date-Local: 2021-01-25 16:33:20
Event-Date-GMT: Mon, 25 Jan 2021 08:33:20 GMT
Event-Date-Timestamp: 1611563600014063
Event-Calling-File: mod_asr.cpp
Event-Calling-Function: onAsrSentenceEnd
Event-Calling-Line-Number: 215
Event-Sequence: 2485
UUID: 8a53b863-e6fc-46e1-902b-7b1931c47164
ASR-Response: {"header":{"namespace":"SpeechTranscriber","name":"SentenceEnd","status":20000000,"message_id":"0ca84cbeed884ca39c88c0c5ae4edbb4","task_id":"97f77f8f53f14eef8be5469375051d81","status_text":"Gateway:SUCCESS:Success."},"payload":{"index":3,"time":4950,"result":"你好。","confidence":0.38665345311164856,"words":[],"status":20000000,"gender":"","begin_time":3600,"stash_result":{"sentenceId":0,"beginTime":0,"text":"","currentTime":0},"audio_extra_info":"","sentence_id":"92887f8e4444437598baeb3768e87035","gender_score":0.0}}
Channel: sofia/internal/8000@cc.com


RECV EVENT
Event-Subclass: stop_asr
Event-Name: CUSTOM
Core-UUID: dbc6fb6a-16e6-44cb-8be8-a49397cc3c5f
FreeSWITCH-Hostname: telegant
FreeSWITCH-Switchname: telegant
FreeSWITCH-IPv4: 10.10.16.180
FreeSWITCH-IPv6: ::1
Event-Date-Local: 2021-01-25 16:34:13
Event-Date-GMT: Mon, 25 Jan 2021 08:34:13 GMT
Event-Date-Timestamp: 1611563653232813
Event-Calling-File: mod_asr.cpp
Event-Calling-Function: onAsrChannelClosed
Event-Calling-Line-Number: 331
Event-Sequence: 2495

RECV EVENT
Event-Subclass: update_asr
Event-Name: CUSTOM
Core-UUID: dbc6fb6a-16e6-44cb-8be8-a49397cc3c5f
FreeSWITCH-Hostname: telegant
FreeSWITCH-Switchname: telegant
FreeSWITCH-IPv4: 10.10.16.180
FreeSWITCH-IPv6: ::1
Event-Date-Local: 2021-01-25 16:34:50
Event-Date-GMT: Mon, 25 Jan 2021 08:34:50 GMT
Event-Date-Timestamp: 1611563690152634
Event-Calling-File: mod_asr.cpp
Event-Calling-Function: onAsrTranscriptionResultChanged
Event-Calling-Line-Number: 249
Event-Sequence: 2512
UUID: 8a53b863-e6fc-46e1-902b-7b1931c47164
ASR-Response: {"header":{"namespace":"SpeechTranscriber","name":"TranscriptionResultChanged","status":20000000,"message_id":"06bf1659fc904abcb95c727e7fb143a2","task_id":"3d7563f486a74aa28b3d50256eae0958","status_text":"Gateway:SUCCESS:Success."},"payload":{"index":1,"time":6340,"result":"结果是100还是150 ？","confidence":0.45189642906188965,"words":[],"status":20000000}}
Channel: sofia/internal/8000@cc.com

```

ASR-Response: asr识别返回结果 Channel: 当前Channel Name 

