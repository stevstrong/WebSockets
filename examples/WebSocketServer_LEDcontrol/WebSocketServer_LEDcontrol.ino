/*
 * WebSocketServer_LEDcontrol.ino
 *
 *  Created on: 25.02.2018
 *
 */

#include <WebSocketsServer.h>
#include <Streaming.h>


char dbg[500];

#define PRINTF(...) { Serial.print(millis()); Serial.write(' '); sprintf(dbg, __VA_ARGS__); Serial.print(dbg); }


EthernetServer server(80);
WebSocketsServer webSocket(81);

//-----------------------------------------------------------------------------
const char header_htm[]="HTTP/1.1 200 OK\r\n"
						"Content-type: text/html\r\n"
						"Connection: close\r\n\r\n";

const char header_icon[]="HTTP/1.1 200 OK\r\n"
						"Content-type: image/x-icon\r\n\r\n";

const char header_NOK[]="HTTP/1.1 404 Not Found\r\n"
						"Content-type: text/plain\r\n"
						"Connection: close\r\n\r\n"
						"page not found.\r\n";
const char index_htm[]={
#include "index_htm.h"
};

#include "favicon.h"

//-----------------------------------------------------------------------------
int rxCount = 0;
bool wsConnected = false;
uint8_t wsNum = 0;
uint32_t z, onTime = 250, offTime = 250;
//-----------------------------------------------------------------------------
void blink()
{
	if ( digitalRead(LED_BUILTIN) )
	{ // LED is off, switch it on if it's time
		if ( (millis()-z)>=offTime )
		{
			z = millis();
			digitalWrite(LED_BUILTIN, 0);
			if ( wsConnected ) webSocket.sendTXT(wsNum, "LED=1");
		}
	}
	else
	{ // LED is on, switch it off if it's time
		if ( (millis()-z)>=onTime )
		{
			z = millis();
			digitalWrite(LED_BUILTIN, 1);
			if ( wsConnected ) webSocket.sendTXT(wsNum, "LED=0");
		}
	}
}
//-----------------------------------------------------------------------------
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
	switch(type)
	{
		case WStype_DISCONNECTED:
		{
			PRINTF("[WEBSOCKET][%u] Disconnected!\n", num);
			wsConnected = false;
			break;
		}
		case WStype_CONNECTED:
		{
			wsConnected = true;
			wsNum = num;
			IPAddress ip = webSocket.getRemoteIP(num);
			PRINTF("[WEBSOCKET][%u] Connected to client at %d.%d.%d.%d, url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
			// send message to client
			webSocket.sendTXT(num, "Alive! onTime=250; offTime=250;");
			break;
		}
		case WStype_TEXT:
		{
			rxCount++;
			PRINTF("[WEBSOCKET][%u] Rx %u bytes: %s\n", num, length, payload);

			if ( strncmp((const char*)payload, "onTime", 6)==0 )
			{
				if (payload[6]=='=')
					onTime = atoi((const char*)&payload[7]);
			}
			else if ( strncmp((const char*)payload, "offTime", 7)==0 )
			{
				if (payload[7]=='=')
					offTime = atoi((const char*)&payload[8]);
			}
			break;
		}
		case WStype_ERROR:
		default:
			break;
	}
}
//-----------------------------------------------------------------------------
const uint8_t ETHERNET_SPI_CS_PIN = PA4;

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network.
// gateway and subnet are optional:
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

IPAddress ip(192,168,1,102);
//-----------------------------------------------------------------------------
void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, 1);

    Serial.begin(115200);
	while (!Serial); delay(10);

	Serial.println("*************************************************************");
	Serial.println("***** Web socket server example *****");
	Serial.println("*************************************************************");

	Serial.println("\nInitializing the Ethernet device...");
	// init Ethernet interface
	Ethernet.init(ETHERNET_SPI_CS_PIN);

	Serial.print("\nGetting IP address using DHCP...");
	if ( Ethernet.begin(mac, 5000)==0 ) {
		Serial.print("failed! ");
		Serial.print("Getting static IP address...");
		Ethernet.begin(mac, ip);
	}
	Serial.println("done.");

	server.begin();
	Serial << ("HTTP Server is at ") << Ethernet.localIP() << endl << endl;

    // start webSocket server
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
}
//-----------------------------------------------------------------------------
void loop()
{
	ListenForClient();

	webSocket.loop();

	blink();
}
//-----------------------------------------------------------------------------
uint8_t rxFlush;
EthernetClient client;
//-----------------------------------------------------------------------------
void ListenForClient(void)
{
	client = server.available();
	if ( !client ) return;
  
    if (client.connected())
	{
		Serial << millis() << "> Client connected.\n\n";
		uint16_t indx = 0;
		rxFlush = 0;
		while (client.available())
		{
			char c = client.read();
			Serial.write(c);
			if ( rxFlush ||  c=='\r' ) continue; // don't save CR character
			if ( c=='\n' ) c = 0;
			dbg[indx++] = c;
			if (c==0) //  end of a line
			{
				HTTP_ParseRequest(dbg);	// parse request
			}
		}
		HTTP_PostProcess(); // send any reply if needed
		// close the connection:
		client.stop();
		Serial << millis() << "> Client disconnected.\n\n";
	}
}
//-----------------------------------------------------------------------------
typedef enum 
{
	SEND_NOTHING = 0,
	SEND_INDEX_PAGE,
	SEND_FAVICON,
	SEND_NOT_FOUND
} txMode_t;
txMode_t txMode;
//-----------------------------------------------------------------------------
void HTTP_ParseRequest(char * p)
{
	if ( p[0]=='G' && p[1]=='E' && p[2]=='T' ) // GET
	{
		// get request data
		if ( (p = strtok(p+4, " "))==NULL ) {
			Serial << "Wrong HTTP request format!\n"; return; }
		// now we have the request payload
		p++;
		if ( *p==0 ) // GET / HTTP/1.1 = index page request
		{
			txMode = SEND_INDEX_PAGE;
		}
		else
		if ( strncmp(p, "favicon.ico", 11)==0 )
		{
			txMode = SEND_FAVICON;
		}
		else
		{
			txMode = SEND_NOT_FOUND;
		}
	}
	if ( txMode>SEND_NOTHING ) rxFlush = true;
}
//-----------------------------------------------------------------------------
void HTTP_PostProcess(void)
{
	if ( txMode>SEND_NOTHING && client )
	{
		if ( txMode==SEND_INDEX_PAGE )
		{
			client.write(header_htm, sizeof(header_htm)-1); // OK header
			uint32_t t = millis();
			Serial << t << "> Tx: sending Index page in ";
			client.write(index_htm, sizeof(index_htm)-1);//, sizeof(index_htm));
			Serial << ( millis()-t ) << " ms.\n";
		}
		else
		if ( txMode==SEND_FAVICON )
		{
			client.write(header_icon, sizeof(header_icon)-1); // icon header
			uint32_t t = millis();
			Serial << t << "> Tx: sending favicon in ";
			client.write(favicon_ico, sizeof(favicon_ico)-1);//, sizeof(favicon_ico));
			Serial << ( millis()-t ) << " ms.\n";
		}
		else
		if ( txMode==SEND_NOT_FOUND )
		{
			client.write(header_NOK, sizeof(header_NOK)-1);
		}
		txMode = SEND_NOTHING;
	}
}
