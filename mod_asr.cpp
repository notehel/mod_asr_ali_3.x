#include <switch.h>

#include <fstream>
#include "nlsClient.h"
#include "nlsEvent.h"
#include "speechTranscriberRequest.h"
#include "nlsCommonSdk/Token.h"
#include <sys/time.h>


#include <curl/curl.h>



#define MAX_FRAME_BUFFER_SIZE (1024*1024) //1MB
#define SAMPLE_RATE 8000

using namespace AlibabaNlsCommon;
using AlibabaNls::NlsClient;
using AlibabaNls::NlsEvent;
using AlibabaNls::LogDebug;
using AlibabaNls::SpeechTranscriberRequest;


struct AsrParamCallBack {
    std::string caller;
    std::string callee;
	char *sUUID ;

};



bool postResult(const char *jsonObj, AsrParamCallBack *cbParam  )
{
    CURL *curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (curl == NULL) {
        return false;
    }

   char str1[8000];




   sprintf(str1, "{\"call_info\":{\"call_id\": \"%s\",\"caller\": \"%s\",\"callee\": \"%s\"},\"asr_result\": %s}", cbParam->sUUID,cbParam->caller.c_str(),cbParam->callee.c_str(),jsonObj);





    struct curl_slist *headers = NULL;
    curl_slist_append(headers, "Accept: application/json");
    curl_slist_append(headers, "Content-Type: application/json");
    curl_slist_append(headers, "charset: utf-8");

    curl_easy_setopt(curl, CURLOPT_URL, "http://10.10.22.189/asr_event");

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, str1);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "post url %s:%s\n", "http://10.10.22.189/asr_event",jsonObj);


    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return res;
}



//======================================== ali asr start ===============


typedef struct {

    switch_core_session_t   *session;
    switch_media_bug_t      *bug;
    SpeechTranscriberRequest *request;

    int                     started;
    int                     stoped;
    int                     starting;

    int                     datalen;

    switch_mutex_t          *mutex;
    switch_memory_pool_t *pool;

    switch_audio_resampler_t *resampler;


} switch_da_t;

std::string g_appkey = "";
std::string g_akId = "";
std::string g_akSecret = "";
std::string g_token = "";
long g_expireTime = -1;

SpeechTranscriberRequest* generateAsrRequest(AsrParamCallBack * cbParam);

int generateToken(std::string akId, std::string akSecret, std::string* token, long* expireTime) {
    NlsToken nlsTokenRequest;
    nlsTokenRequest.setAccessKeyId(akId);
    nlsTokenRequest.setKeySecret(akSecret);


    if (-1 == nlsTokenRequest.applyNlsToken()) {
         switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "generateToken Failed: %s\n", nlsTokenRequest.getErrorMsg());

        return -1;
    }


    *token = nlsTokenRequest.getToken();
    *expireTime = nlsTokenRequest.getExpireTime();


    return 0;
}


void onTranscriptionStarted(NlsEvent* cbEvent, void* cbParam) {
   AsrParamCallBack* tmpParam = (AsrParamCallBack*)cbParam;

   switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "onAsrTranscriptionStarted: %s\n", tmpParam->sUUID);
   switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"onAsrTranscriptionStarted: status code=%d, task id=%s\n", cbEvent->getStatusCode(), cbEvent->getTaskId());

   switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"onAsrTranscriptionStarted: all response=%s\n", cbEvent->getAllResponse());

   switch_da_t *pvt;
   switch_core_session_t *ses = switch_core_session_force_locate(tmpParam->sUUID);
   switch_channel_t *channel = switch_core_session_get_channel(ses);
   if((pvt = (switch_da_t*)switch_channel_get_private(channel, "asr")))
   {

      switch_mutex_lock(pvt->mutex);
      pvt->started = 1;
      pvt->starting = 0;
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"I need lock!!!!!!!!!!!! \n"  );

      switch_mutex_unlock(pvt->mutex);

   }
}



void onAsrSentenceBegin(NlsEvent* cbEvent, void* cbParam) {
	AsrParamCallBack* tmpParam = (AsrParamCallBack*)cbParam;
     switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"onAsrSentenceBegin: %s\n", tmpParam->sUUID);
     switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"onAsrSentenceBegin: status code=%d, task id=%s, index=%d, time=%d\n", cbEvent->getStatusCode(), cbEvent->getTaskId(),
                cbEvent->getSentenceIndex(),
                cbEvent->getSentenceTime());
}

void onAsrSentenceEnd(NlsEvent* cbEvent, void* cbParam) {
	AsrParamCallBack* tmpParam = (AsrParamCallBack*)cbParam;
     switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"onAsrSentenceEnd: %s\n", tmpParam->sUUID);
     switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"onAsrSentenceEnd: status code=%d, task id=%s, index=%d, time=%d, begin_time=%d, result=%s\n", cbEvent->getStatusCode(), cbEvent->getTaskId(),
                cbEvent->getSentenceIndex(),
                cbEvent->getSentenceTime(),
                cbEvent->getSentenceBeginTime(),
                cbEvent->getResult()
                );
       switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "onAsrSentenceEnd: all response=%s\n", cbEvent->getAllResponse());
       switch_event_t *event = NULL;
       switch_core_session_t *ses = switch_core_session_force_locate(tmpParam->sUUID);
       switch_channel_t *channel = switch_core_session_get_channel(ses);
       if(switch_event_create(&event, SWITCH_EVENT_CUSTOM) == SWITCH_STATUS_SUCCESS) {
            event->subclass_name = strdup("start_asr");
            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Event-Subclass", event->subclass_name);
            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "UUID", tmpParam->sUUID);
            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "ASR-Response", cbEvent->getAllResponse());
            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Channel", switch_channel_get_name(channel));
            //switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Timestamp",currtime);
            //switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Answered",answered);
            switch_event_fire(&event);
       }

       postResult(cbEvent->getAllResponse(),tmpParam);

}

void onAsrTranscriptionResultChanged(NlsEvent* cbEvent, void* cbParam) {
	AsrParamCallBack* tmpParam = (AsrParamCallBack*)cbParam;
     switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"onAsrTranscriptionResultChanged: %s\n", tmpParam->sUUID);
     switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"onAsrTranscriptionResultChanged: status code=%d, task id=%s, index=%d, time=%d, result=%s\n", cbEvent->getStatusCode(), cbEvent->getTaskId(),
                cbEvent->getSentenceIndex(),
                cbEvent->getSentenceTime(),
                cbEvent->getResult()
                );
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "onAsrTranscriptionResultChanged: all response=%s\n", cbEvent->getAllResponse());

    switch_event_t *event = NULL;
    switch_core_session_t *ses = switch_core_session_force_locate(tmpParam->sUUID);
    switch_channel_t *channel = switch_core_session_get_channel(ses);
    if (switch_event_create(&event, SWITCH_EVENT_CUSTOM) == SWITCH_STATUS_SUCCESS) {
        event->subclass_name = strdup("update_asr");
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Event-Subclass", event->subclass_name);
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "UUID", tmpParam->sUUID);
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "ASR-Response", cbEvent->getAllResponse());
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Channel", switch_channel_get_name(channel));
        //switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Timestamp",currtime);
        //switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Answered",answered);
        switch_event_fire(&event);
    }
    postResult(cbEvent->getAllResponse(),tmpParam);
}

void onAsrTranscriptionCompleted(NlsEvent* cbEvent, void* cbParam) {
	AsrParamCallBack* tmpParam = (AsrParamCallBack*)cbParam;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"onAsrTranscriptionCompleted: %s\n", tmpParam->sUUID);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"onAsrTranscriptionCompleted: status code=%d, task id=%s\n", cbEvent->getStatusCode(), cbEvent->getTaskId());

    switch_da_t *pvt;
    switch_core_session_t *ses = switch_core_session_force_locate(tmpParam->sUUID);
    switch_channel_t *channel = switch_core_session_get_channel(ses);
    if((pvt = (switch_da_t*)switch_channel_get_private(channel, "asr")))
    {
//        if(pvt->frameDataBuffer){
//            free(pvt->frameDataBuffer);
//        }
    }
}

void onAsrTaskFailed(NlsEvent* cbEvent, void* cbParam) {
	AsrParamCallBack* tmpParam = (AsrParamCallBack*)cbParam;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"onAsrTaskFailed: %s\n", tmpParam->sUUID);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"onAsrTaskFailed: status code=%d, task id=%s, error message=%s\n", cbEvent->getStatusCode(), cbEvent->getTaskId(), cbEvent->getErrorMessage());
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "onAsrTaskFailed: all response=%s\n", cbEvent->getAllResponse());

    switch_da_t *pvt;
    switch_core_session_t *ses = switch_core_session_force_locate(tmpParam->sUUID);
    switch_channel_t *channel = switch_core_session_get_channel(ses);
    if((pvt = (switch_da_t*)switch_channel_get_private(channel, "asr")))
    {
        switch_mutex_lock(pvt->mutex);
        pvt->started = 0;
        switch_mutex_unlock(pvt->mutex);

    }
}

void onAsrSentenceSemantics(NlsEvent* cbEvent, void* cbParam) {
    AsrParamCallBack* tmpParam = (AsrParamCallBack*)cbParam;
     switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"onAsrSentenceSemantics: %s\n", tmpParam->sUUID);
     switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,"onAsrSentenceSemantics: all response=%s\n", cbEvent->getAllResponse());
}

void onAsrChannelClosed(NlsEvent* cbEvent, void* cbParam) {

	AsrParamCallBack* tmpParam = (AsrParamCallBack*)cbParam;
    switch_event_t *event = NULL;
    if(switch_event_create(&event, SWITCH_EVENT_CUSTOM) == SWITCH_STATUS_SUCCESS) {
        event->subclass_name = strdup("stop_asr");
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Event-Subclass", event->subclass_name);
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "ASR-Close", cbEvent->getResult());
        switch_event_fire(&event);
    }
    delete tmpParam;
}



SpeechTranscriberRequest* generateAsrRequest(AsrParamCallBack * cbParam) {


    time_t now;

    time(&now);

    if (g_expireTime - now < 10) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "the token will be expired, please generate new token by AccessKey-ID and AccessKey-Secret\n");

        if (-1 == generateToken(g_akId, g_akSecret, &g_token, &g_expireTime)) {
            return NULL;
        }
    }




    SpeechTranscriberRequest* request = NlsClient::getInstance()->createTranscriberRequest();
    if (request == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "createTranscriberRequest failed.\n" );

        return NULL;
    }

    request->setOnTranscriptionStarted(onTranscriptionStarted, cbParam);                // 设置识别启动回调函数
    request->setOnTranscriptionResultChanged(onAsrTranscriptionResultChanged, cbParam);    // 设置识别结果变化回调函数
    request->setOnTranscriptionCompleted(onAsrTranscriptionCompleted, cbParam);            // 设置语音转写结束回调函数
    request->setOnSentenceBegin(onAsrSentenceBegin, cbParam);                              // 设置一句话开始回调函数
    request->setOnSentenceEnd(onAsrSentenceEnd, cbParam);                                  // 设置一句话结束回调函数
    request->setOnTaskFailed(onAsrTaskFailed, cbParam);                                    // 设置异常识别回调函数
    request->setOnChannelClosed(onAsrChannelClosed, cbParam);                              // 设置识别通道关闭回调函数
    request->setOnSentenceSemantics(onAsrSentenceSemantics, cbParam);                      //设置二次结果返回回调函数, 开启enable_nlp后返回

    request->setAppKey(g_appkey.c_str());            // 设置AppKey, 必填参数, 请参照官网申请

    request->setFormat("pcm");                          // 设置音频数据编码格式, 默认是pcm
    request->setSampleRate(SAMPLE_RATE);                // 设置音频数据采样率, 可选参数，目前支持16000, 8000. 默认是16000
    request->setIntermediateResult(true);               // 设置是否返回中间识别结果, 可选参数. 默认false
    request->setPunctuationPrediction(true);            // 设置是否在后处理中添加标点, 可选参数. 默认false
    request->setInverseTextNormalization(true);         // 设置是否在后处理中执行数字转写, 可选参数. 默认false

    request->setToken(g_token.c_str());


    return request;


}



//======================================== ali asr end ===============


//======================================== freeswitch module start ===============


SWITCH_MODULE_LOAD_FUNCTION(mod_asr_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_asr_shutdown);

extern "C" {
     SWITCH_MODULE_DEFINITION(mod_asr, mod_asr_load, mod_asr_shutdown, NULL);
};



static switch_status_t load_config()
{
	const char *cf = "aliasr.conf";
	switch_xml_t cfg, xml, settings, param;

 	if (!(xml = switch_xml_open_cfg(cf, &cfg, NULL))) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Open of %s failed\n", cf);
		switch_xml_free(xml);
		return SWITCH_STATUS_TERM;
	}

	if ((settings = switch_xml_child(cfg, "settings"))) {
		for (param = switch_xml_child(settings, "param"); param; param = param->next) {
			char *var = (char *) switch_xml_attr_soft(param, "name");
			char *val = (char *) switch_xml_attr_soft(param, "value");
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "read conf %s %s\n", var,val);

			if (!strcasecmp(var, "appkey")) {
			    g_appkey = val;
			}else if (!strcasecmp(var, "akid")) {
				 g_akId =  val;


			}else if (!strcasecmp(var, "aksecret")) {

			     g_akSecret=  val;

             }
		}
	}

	return SWITCH_STATUS_SUCCESS;

}




static switch_bool_t asr_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type)
{
    switch_da_t *pvt = (switch_da_t *)user_data;
    switch_channel_t *channel = switch_core_session_get_channel(pvt->session);

    switch (type) {
        case SWITCH_ABC_TYPE_INIT:
        {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "ASR Channel Init:%s\n", switch_channel_get_name(channel));
        }
        break;
        case SWITCH_ABC_TYPE_CLOSE:
        {
            if (pvt->request) {

                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "ASR Stop Succeed channel: %s\n", switch_channel_get_name(channel));

                pvt->request->stop();

                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "asr stoped:%s\n", switch_channel_get_name(channel));

                //7: 识别结束, 释放request对象
                NlsClient::getInstance()->releaseTranscriberRequest(pvt->request);

                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "asr released:%s\n", switch_channel_get_name(channel));

            }
        }
        break;
        case SWITCH_ABC_TYPE_WRITE_REPLACE:
        {
            if(pvt->stoped ==1 ){
                return SWITCH_TRUE;
            }

            switch_frame_t *frame= switch_core_media_bug_get_write_replace_frame(bug);
            if (!frame) {
                return SWITCH_TRUE;
            }

            if (frame->channels != 1)
            {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "nonsupport channels:%d!\n",frame->channels);
                return SWITCH_TRUE;
            }

            switch_mutex_lock(pvt->mutex);
            if(pvt->started ==0 ) {

                if(pvt->starting ==0){

                    pvt->starting = 1;
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Starting Transaction \n" );

                    AsrParamCallBack *cbParam  = new AsrParamCallBack;

                    cbParam->sUUID= switch_channel_get_uuid(channel);

                    switch_caller_profile_t  *profile = switch_channel_get_caller_profile(channel);

                    cbParam->caller = profile->caller_id_number;
                    cbParam->callee = profile->callee_id_number;

                    SpeechTranscriberRequest* request = generateAsrRequest(cbParam);

                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Caller %s. Callee %s\n",cbParam->caller.c_str() , cbParam->callee.c_str() );

                    if(request == NULL){
                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Asr Request init failed.%s\n", switch_channel_get_name(channel));

                         return SWITCH_TRUE;
                    }

                    pvt->request = request;

                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Init SpeechTranscriberRequest.%s\n", switch_channel_get_name(channel));

                    if (pvt->request->start() < 0) {

                       pvt->stoped = 1;

                       switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "start() failed. may be can not connect server. please check network or firewalld:%s\n", switch_channel_get_name(channel));

                       NlsClient::getInstance()->releaseTranscriberRequest(pvt->request); // start()失败，释放request对象
                   }
                }

            }else {
                //====== resample ==== ///

                switch_codec_implementation_t read_impl;
                memset(&read_impl, 0, sizeof(switch_codec_implementation_t));

                switch_core_session_get_read_impl(pvt->session, &read_impl);


                int datalen = frame->datalen;
                int16_t *dp = (int16_t *)frame->data;

                switch_core_media_bug_set_write_replace_frame(bug, frame);

                if (read_impl.actual_samples_per_second != 8000) {
                    if (!pvt->resampler) {
                        if (switch_resample_create(&pvt->resampler,
                                                   read_impl.actual_samples_per_second,
                                                   8000,
                                                   8 * (read_impl.microseconds_per_packet / 1000) * 2,
                                                   SWITCH_RESAMPLE_QUALITY,
                                                   1) != SWITCH_STATUS_SUCCESS) {
                            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Unable to allocate resampler\n");
                            return SWITCH_FALSE;
                        }
                    }

                    switch_resample_process(pvt->resampler, dp, (int) datalen / 2 / 1);
                    memcpy(dp, pvt->resampler->to, pvt->resampler->to_len * 2 * 1);
                    int samples = pvt->resampler->to_len;
                    datalen = pvt->resampler->to_len * 2 * 1;

                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "ASR new samples:%d\n", samples);

                }


                if (pvt->request->sendAudio((uint8_t *)dp, (size_t)datalen) <0) {
                    pvt->stoped =1;
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "send audio failed:%s\n", switch_channel_get_name(channel));
                    pvt->request->stop();
                    NlsClient::getInstance()->releaseTranscriberRequest(pvt->request);
                }

            }

            switch_mutex_unlock(pvt->mutex);

        }
        break;
        default:
        break;
    }

    return SWITCH_TRUE;
}


SWITCH_STANDARD_APP(stop_asr_session_function)
{
    switch_da_t *pvt;
    switch_channel_t *channel = switch_core_session_get_channel(session);

    if ((pvt = (switch_da_t*)switch_channel_get_private(channel, "asr"))) {

        switch_channel_set_private(channel, "asr", NULL);
        switch_core_media_bug_remove(session, &pvt->bug);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "%s Stop ASR\n", switch_channel_get_name(channel));

    }
}


SWITCH_STANDARD_APP(start_asr_session_function)
{


    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Starting asr:%s\n", switch_channel_get_name(channel));

    switch_status_t status;
    switch_da_t *pvt;
    switch_codec_implementation_t read_impl;
    memset(&read_impl, 0, sizeof(switch_codec_implementation_t));

    switch_core_session_get_read_impl(session, &read_impl);

    if (!(pvt = (switch_da_t*)switch_core_session_alloc(session, sizeof(switch_da_t)))) {

        return;
    }

    pvt->started = 0;
    pvt->stoped = 0;
    pvt->starting = 0;
    pvt->datalen = 0;
    pvt->session = session;


    if ((status = switch_core_new_memory_pool(&pvt->pool)) != SWITCH_STATUS_SUCCESS) {
       switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Memory Error!\n");

       return;
    }

    switch_mutex_init(&pvt->mutex,SWITCH_MUTEX_NESTED,pvt->pool);



    if ((status = switch_core_media_bug_add(session, "asr", NULL,
        asr_callback, pvt, 0, SMBF_WRITE_REPLACE |  SMBF_NO_PAUSE | SMBF_ONE_ONLY, &(pvt->bug))) != SWITCH_STATUS_SUCCESS) {
        return;
    }

    switch_channel_set_private(channel, "asr", pvt);
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Start ASR\n", switch_channel_get_name(channel));


}






SWITCH_MODULE_LOAD_FUNCTION(mod_asr_load)
{


    if (load_config() != SWITCH_STATUS_SUCCESS) {
    		return SWITCH_STATUS_FALSE;
    }

    int ret = NlsClient::getInstance()->setLogConfig("log-transcriber", LogDebug);
    if (-1 == ret) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "set log failed\n");
        return SWITCH_STATUS_FALSE;
    }

    NlsClient::getInstance()->startWorkThread(4);

    switch_application_interface_t *app_interface;

    *module_interface = switch_loadable_module_create_module_interface(pool, modname);

    SWITCH_ADD_APP(app_interface, "start_asr", "asr", "asr",start_asr_session_function, "", SAF_MEDIA_TAP);
    SWITCH_ADD_APP(app_interface, "stop_asr", "asr", "asr", stop_asr_session_function, "", SAF_NONE);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, " asr_load\n");

    return SWITCH_STATUS_SUCCESS;
}


SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_asr_shutdown)
{
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, " asr_shutdown\n");

    return SWITCH_STATUS_SUCCESS;
}


