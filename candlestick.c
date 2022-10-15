 

#include <libwebsockets.h>
#include <string.h>
#include <time.h>
#include <signal.h>

 
static int interrupted, bad = 1, status, conmon, close_after_start;
 
static struct lws *client_wsi;
static const char *ba_user, *ba_password; 
char* companySymbol ;
char* initialTimestamp ;
char* endTimestamp ;

static const lws_retry_bo_t retry = {
	.secs_since_valid_ping = 3,
	.secs_since_valid_hangup = 10,
};

double maxDelay =0;
  
char* timestampToHumanReadableDate(char* timestampString);

void writeTextToFile(char fileName[], char content[]){ 
        FILE * fptr; 
        int sizeOfString = strlen(content);    
        int fileNameSize = strlen(fileName)+1;  
 
        char fileNameCustom[fileNameSize];  
        strncpy(fileNameCustom, fileName, fileNameSize);
        fileNameCustom[fileNameSize - 1] = '\0';   
        fptr = fopen(fileNameCustom,"a");     
        for (int i = 0; i< sizeOfString ; i++) {   
            fputc(content[i], fptr);
        }
        fputc('\n', fptr);
        fputc('\0', fptr);
        fclose(fptr); 
}

char* concatenateStringWithFloatString(char * concatenationResult,char * str1,char * str2) {      
       double numberString = strtod(str2,NULL); 
       sprintf(concatenationResult, "%s%f", str1, numberString); 
       return concatenationResult; 
 }

void writeFormattedCandleStickDataToFile(char fileName[], char data[], char key[], char value[]  ){  
    if(value!=NULL){ 
            int len1 = strlen(key);
            int len2 = strlen(value);   
	    char * concatenationResult = (char *) malloc(strlen(key)+ strlen(value) +1); 
            concatenateStringWithFloatString(concatenationResult,key, value);  
            writeTextToFile(fileName, concatenationResult); 

	    free(concatenationResult);
     }else if(strstr(key,  "close price")){ 
	// we have this if check for key containing substring "close price"
	// because if no data is returned after stockcandle request, we want only one "no data" message
	// to be written to file, whereas this writeFormattedCandleStickDataToFile method is called multiple times
	// for multiple keys.
	
	// no data found for this symbol for this minute. Write "no data" to candlestick file
	writeTextToFile(fileName, data);
     }
} 
const char* getValueOfKey(char data[], char keyName[] ){
        const char *cs;
        size_t cslen;
        cs = lws_json_simple_find(data, strlen(data), keyName, &cslen);
        if (!cs) {
                lwsl_err("%s: simple_find failed for key %s and data %s\n", __func__, keyName, data); 
                return NULL;
        } 
        char fileName[cslen-1]; 
        strncpy(fileName, cs, cslen); 
        char *result = &fileName[0];  
        return result;
}

void writeKeyValueToFile(char* fileNameString ,char data[], char closePriceKey[], char key[]){ 
        char* closePriceValue =  getValueOfKey((char *)data, closePriceKey);
        writeFormattedCandleStickDataToFile(fileNameString, data, key, closePriceValue);     
}

char* timestampToHumanReadableDate(char* timestampString){
	char  buf[80];  
	struct tm time;
	printf("\n\n TIMESTAMP %s \n\n\n", timestampString);
	time_t loctime = (time_t) strtol(timestampString, NULL, 10); 
        struct tm *tmp;
	tmp = localtime( &loctime); 
        strftime(buf, sizeof(buf), "%x - %I:%M%p", tmp); 
         
	char *result = &buf[0];  
	return result;
}
 
void writeDataToFile(char data[]){   
	char *buf;
	buf = timestampToHumanReadableDate(initialTimestamp); 
	char fileName[80] = "candleStickData-";
	strcat(fileName, companySymbol); 
        char header[120] = "Candlestick for minute starting at: ";
        strcat(header, buf);
        writeTextToFile(fileName, header); 
	writeKeyValueToFile(fileName, data, "\"c\":[", "close price: ");
	writeKeyValueToFile(fileName, data, "\"h\":[", "high price :");
	writeKeyValueToFile(fileName, data, "\"l\":[", "low price :");    
} 

static int callback_http(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
	switch (reason) {  
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		lwsl_err("CLIENT_CONNECTION_ERROR: %s\n",  in ? (char *)in : "(null)");
		interrupted = 1;
		bad = 3; /* connection failed before we could make connection */
		lws_cancel_service(lws_get_context(wsi)); 
		break; 
	case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
		{
			char buf[128];
			lws_get_peer_simple(wsi, buf, sizeof(buf));
			status = (int)lws_http_client_http_response(wsi); 
			lwsl_user("Connected to %s, http response: %d\n", buf, status);
		} 
		if (lws_fi_user_wsi_fi(wsi, "user_reject_at_est"))
			return -1;

		break; 
	case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:{
		clock_t begin = clock();
	        lwsl_user("RECEIVE_CLIENT_HTTP_READ: read %d\n", (int)len);
		int lenSize = (int)len;
		char dotstar[lenSize];
		lws_strnncpy(dotstar, (const char *)in, lenSize,  sizeof(dotstar));  
		 
		writeDataToFile(in);  
		clock_t end = clock();
		double time_spent = (double)(end - begin) / CLOCKS_PER_SEC; 
		char content[80]; 
      		sprintf(content, "delay: %fs", time_spent);   
		writeTextToFile("delays_for_candleStickData", content); 

		char content2[80]; 
      		sprintf(content2, "%f,", time_spent);    
	        writeTextToFile("sum_of_delays_for_candleStickData", content2);  
		return 0;  
	}  
	case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
		lwsl_user("LWS_CALLBACK_COMPLETED_CLIENT_HTTP\n");
		interrupted = 1;
		bad = status != 200;
		lws_cancel_service(lws_get_context(wsi)); /* abort poll wait */
		break;

	case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
		interrupted = 1;
		bad = status != 200;
		lws_cancel_service(lws_get_context(wsi)); /* abort poll wait */ 
		break; 
	default:
		break;
	}

	return lws_callback_http_dummy(wsi, reason, user, in, len);
}

static const struct lws_protocols protocols[] = {
	{
		"http",
		callback_http,
		0, 0, 0, NULL, 0
	},
	LWS_PROTOCOL_LIST_TERM
};

static void
sigint_handler(int sig)
{
	interrupted = 1;
}

struct args {
	int argc;
	const char **argv;
};

static int system_notify_cb(lws_state_manager_t *mgr, lws_state_notify_link_t *link, int current, int target)
{
	struct lws_context *context = mgr->parent;
	struct lws_client_connect_info i;
	struct args *a = lws_context_user(context);
	const char *p;

	if (current != LWS_SYSTATE_OPERATIONAL || target != LWS_SYSTATE_OPERATIONAL)
		return 0;

	lwsl_info("%s: operational\n", __func__);

	memset(&i, 0, sizeof i); /* otherwise uninitialized garbage */
	i.context = context; 

	 
	i.port = 443; 
	i.address=  "finnhub.io"; 
	i.ssl_connection |= LCCSCF_H2_QUIRK_OVERFLOWS_TXCR | LCCSCF_USE_SSL| LCCSCF_ACCEPT_TLS_DOWNGRADE_REDIRECTS | LCCSCF_H2_QUIRK_NGHTTP2_END_STREAM;

	char* endpoint = "/api/v1/stock/candle?symbol="; 
	char* resolutionAndFromQueryFields="&resolution=D&from="; 
	char* toQueryField="&to="; 
	char* tokenQueryField = "&token=ca5l84iad3i4sbn0eskg";
	char buffer[150];

	int max_len = sizeof buffer;

	int j = snprintf(buffer,max_len, endpoint ); 
	strcat(buffer, companySymbol);
	strcat(buffer, resolutionAndFromQueryFields);
	strcat(buffer, initialTimestamp);
	strcat(buffer, toQueryField);
	strcat(buffer, endTimestamp);
	strcat(buffer, tokenQueryField); 
  
	i.alpn = "h2,http/1.1";
	i.path =buffer;  
	i.host = i.address;
	i.origin = i.address;
	i.method = "GET"; 
	i.protocol = protocols[0].name;
	i.pwsi = &client_wsi;
	i.fi_wsi_name = "user";

	if (!lws_client_connect_via_info(&i)) {
		lwsl_err("Client creation failed\n");
		interrupted = 1;
		bad = 2; /* could not even start client connection */
		lws_cancel_service(context); 
		return 1;
	}

	if (close_after_start)
		lws_wsi_close(client_wsi, LWS_TO_KILL_SYNC);

	return 0;
}
 

 
int main(int argc, const char **argv)
{

	printf("\n\n============================== calculating candlestick data=======================\n\n"); 
	companySymbol =argv[1]; // AMZN 1572651390 1575243390
       
	initialTimestamp =argv[2]; 
	endTimestamp= argv[3]; 
 
	printf("\n\n==============================for %s , %s=======================\n\n", initialTimestamp, endTimestamp); 
	lws_state_notify_link_t notifier = { { NULL, NULL, NULL },   system_notify_cb, "app" };
	lws_state_notify_link_t *na[] = { &notifier, NULL };
	struct lws_context_creation_info info;
	struct lws_context *context;
	int n = 0, expected = 0;
	struct args args;
	const char *p; 

	args.argc = argc;
	args.argv = argv;

	signal(SIGINT, sigint_handler);

	memset(&info, 0, sizeof info); /* otherwise uninitialized garbage */
	lws_cmdline_option_handle_builtin(argc, argv, &info);

	lwsl_user("LWS minimal http client [-d<verbosity>] [-l] [--h1]\n");

	info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT | LWS_SERVER_OPTION_H2_JUST_FIX_WINDOW_UPDATE_OVERFLOW;
	info.port = CONTEXT_PORT_NO_LISTEN; /* we do not run any server */
	info.protocols = protocols;
	info.user = &args;
	info.register_notifier_list = na;
	info.connect_timeout_secs = 30;  
	info.fd_limit_per_thread = 1 + 1 + 1;

	 
 
	context = lws_create_context(&info);
	if (!context) {
		lwsl_err("lws init failed\n");
		bad = 5;
		goto bail;
	}

	while (n >= 0 && !interrupted) n = lws_service(context, 0);

	lws_context_destroy(context);

	return 0;	
bail:   
	if (bad == expected) {
		lwsl_user("Completed: OK (seen expected %d)\n", expected);
		return 0;
	} else
		lwsl_err("Completed: failed: exit %d, expected %d\n", bad, expected);

	return 1;
}
