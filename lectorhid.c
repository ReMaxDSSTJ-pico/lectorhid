// lectorhid.c
// Para leer las tarjetas PCPROX que se ven como HID Keyboard, pero no envían los datos como si fuera teclado
// Lector de tarjetas RFIDEAS WAVE ID Solo, el modelo SDK RDR-6082AKU
// Pedro Farías Lozano
// Marzo 2024
// Based on phyton work from     micolous/pcprox
// https://github.com/micolous/pcprox
// https://micolous.id.au/
//
// Based on HIDAPI library by 
// libusb Public 	A cross-platform library to access USB devices 
// normally this is obtained from git tags and filled out by the Makefile
#ifndef LECTORHID_VERSION
#define LECTORHID_VERSION "v1.0"
#endif

#define PCPROX_VENDOR  0x0c27	// RFIDEAS
#define PCPROX_PRODUCT 0x3bfa	// PCPROX 125KHZ HID Keyboard Reader

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <getopt.h>
#include <unistd.h>
#include <wchar.h> // wchar_t
#include <hidapi.h>
#include <conio.h>

#define MAX_STR 255
#define MAX_BUF 1024  // for buf reads & writes


//unsigned char NULLMSG[8]={0,0,0,0,0,0,0,0};
unsigned char PCPROX_READBUFFER[MAX_BUF];
unsigned char card_data[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
unsigned char card_info[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
unsigned char buf[65];

// local states for the "cmd" option variable
enum {
    CMD_NONE = 0,
    CMD_VERSION,
    CMD_VIDPID,
    CMD_UID20BITS,
    CMD_ONEPASS_READ,
    CMD_TIME_OUT,
};

union QuickConvert {
	unsigned int		iData;
	unsigned char	cBytes[4];
};

union QuickConvert uFAC;		// TAG FAC and UID
union QuickConvert uUID;

int cmd = CMD_NONE;
int timeout_millis = 500;		// wait time between TAG reads
time_t timeout_wait_seconds=3;	// wait time for one pass read
hid_device *handle = NULL;		// HID API device we will open
bool ONE_PASS_READ=false;
bool UID_20_BITS=false;
bool msg_quiet = false;
bool msg_verbose = false;

int print_base = 16; // 16 or 10, hex or decimal
int print_width = 32; // how many characters per line

/*
  Convenience function to find a pcProx by its vendor and product ID, then
  open a connection to it.
  Open the PCPROX reader by VID,PID that we know is for RFIDEAS WAVEID reader
  Other models will require changing the PID.
*/
hid_device * Open_PCPROX(uint16_t vidx, uint16_t pidx) {
hid_device *dev;
	if (vidx>0 && pidx>0)	// check for presence of vidx, pidx 
		dev = hid_open(vidx,pidx,NULL);
	else
		dev = hid_open(PCPROX_VENDOR,PCPROX_PRODUCT,NULL);
 return dev;
}

static void print_usage(char *myname)
{
    fprintf(stderr,
"Usage: \n"
"  %s <cmd> [options]\n"
"where <cmd> is one of:\n"
"  --vidpid <vid>:<pid>        Use vid and pid for HID reader."
"  --UID20bits                 Force UID to use 20 data bits.\n"
"  --read-one-tag              Read only one tag and exit. \n"
"  --timeout <msecs>           Timeout in seconds to wait for TAG reads when using --read-one-tag. \n"
"  --version                   Print out lectorhid and hidapi version.\n"
"  --help                      Print this help text.\n"
"  -h                          Same as --help, print this help text.\n"
"  -?                          Same as --help, print this help text.\n"
"\n"
"Notes: \n"
" . Default reader to open is RFIDEAS PCPROX VID:PID 0C27:3BFA WaveID Solo."
" . Commands are executed in order. \n"
" . --vidpid, --UIDbits --FACbits --read-one-tag --timeout --version --help \n"
"\n"
"Examples: \n"
". Open vid/pid xxxx:yyyy reader \n"
"   lectorhid --vidpid 0c27:3bfa \n"
". Force to use 16 bits on UID no matter the bits format \n"
"   lectorhid --UID20bits \n"
". Open vid/pid xxxx:yyyy, do a one pass read and exit\n"
"   lectorhid --vidpid xxxx:yyyy --read-one-tag \n"
". Open vid/pid xxxx:yyyy reader and read tags continuously with 1500 msec timeout \n"
"   lectorhid --vidpid xxxx:yyyy --timeout 1500 \n"
". Print version and help\n"
"   lectorhid --version --help\n"
"\n"
""
"", myname);
}


/* # Sends a message to the PCPROX HID device.
   # This msg needs to be exactly 8 bytes.
*/
int PCPROX_Write(unsigned char *msg) {
int hid_status;

#ifdef PCPROX_DEBUG
	    printf("USB TX: >>> ");
		for (int kk=0; kk < 9; kk++) 
			printf("%02x ",(unsigned char) *(msg+kk));

		printf("\t\t");
#endif
        hid_status=hid_send_feature_report(handle,msg,9);
#ifdef PCPROX_DEBUG
		printf(" HID Return status %i ",hid_status);
#endif
        usleep(1000);	// sleep 1 ms
		return hid_status;
}


/*
        Reads a message from the device as a bytes object.
        All messages are 8 bytes long. ie: len(d.read) == 8.
        If a message of all NULL bytes is returned, then this method will instead
        return None.
*/
unsigned char *PCPROX_Read(){
		int hid_status;

		PCPROX_READBUFFER[0]=0;
		hid_status=hid_get_feature_report(handle,PCPROX_READBUFFER,9);
		if ((hid_status==-1) || hid_status<8) {

#ifdef PCPROX_DEBUG
		printf("\nError reading feature report.");
#endif
			return NULL;
		}
    //    # Feature reports have a report number added to them, skip that.
#ifdef PCPROX_DEBUG
        printf("\nUSB RX: >>> ");
		for (int kk=0; kk < hid_status; kk++) 
			printf("%02X ",(unsigned char)PCPROX_READBUFFER[kk]);
		printf("\n\n");
#endif
        return PCPROX_READBUFFER;
}

/**
 * printf that can be shut up
 */
void msg(char* fmt, ...)
{
    va_list args;
    va_start(args,fmt);
    if(!msg_quiet) { vprintf(fmt,args); }
    va_end(args);
}

/**
 * printf that is wordy
 */
void msginfo(char* fmt, ...)
{
    va_list args;
    va_start(args,fmt);
    if(msg_verbose) { vprintf(fmt,args); }
    va_end(args);
}

/*
        Writes to the device, then reads a message back from it.
*/
unsigned char *interact(unsigned char *msg) {
        PCPROX_Write(msg);
        return PCPROX_Read();
}

/* Read TAG. The first byte is the report number (0x0).
*/
unsigned int PCPROX_READTAGS() {
	unsigned char *hid_call_status;
	unsigned char *read_bits;

	uFAC.iData=0;
	uUID.iData=0;

	// Initialize buffer, should be all 0's
	for (int i=0;i<65;i++) buf[i]=0;
	buf[0] = 0x0;
	buf[1] = 0x8F;

	hid_call_status=interact(buf);

	if (hid_call_status==NULL) {
		printf("\nError en lectura. report is empty.\n");
		return 0;	// Could not read
	}
	for (int kj=0;kj<8;kj++)
		card_data[kj]=PCPROX_READBUFFER[kj];


	buf[0] = 0x0;
	buf[1] = 0x8E;
	hid_call_status=interact(buf);

	if (hid_call_status==NULL) {
		printf("\nError en lectura. report is empty.\n");
		return 0;	// Could not read
	}
	for (int kj=0;kj<8;kj++)
		card_info[kj]=PCPROX_READBUFFER[kj];


	if ((card_data[1] + card_data[2] + card_data[3])>0) {
		printf("\nTAG DATA:");
			for (int kj=0;kj<8;kj++)
				printf("%02X",(char unsigned)card_data[kj]);
			printf("\n");

		printf("TAG INFO:");
			for (int kj=0;kj<8;kj++)
				printf("%02X",(char unsigned)card_info[kj]);
			printf("\n");

		read_bits=card_info+1;	// Point to the 1 byte bit lenght info
		printf("Card has %i data bits.",*read_bits);

		uUID.cBytes[0]=card_data[1];
		uUID.cBytes[1]=card_data[2];
		uUID.cBytes[2]=card_data[3];

		uFAC.cBytes[0]=card_data[3];
		uFAC.cBytes[1]=card_data[4];

		if (UID_20_BITS && (*read_bits)==32) {		// Only if card has 32 bits read
			uUID.cBytes[2]=0x0F && uUID.cBytes[2];	// Mask the bits, discard the high nibble
			uFAC.iData=uFAC.iData >> 4;				// FAC is misaligned, discard 4 LSB
		} else {
			uUID.cBytes[2]=0;						// Discard the bits above 16
													// FAC is aligned, no need to mask or shitf.
		}

		// Check the format of the card
		printf(" FAC=%i UID=%i\n",uFAC.iData,uUID.iData);
	}
	return	uUID.iData;
}

/**
 * Parse a comma-delimited 'string' containing numbers (dec,hex)
 * into a array'buffer' (of element size 'bufelem_size') and
 * of max length 'buflen', using delimiter 'delim_str'
 * Returns number of bytes written
 */
int str2buf(void* buffer, char* delim_str, char* string, int buflen, int bufelem_size)
{
    char    *s;
    int     pos = 0;
    if( string==NULL ) return -1;
    memset(buffer,0,buflen);  // bzero() not defined on Win32?
    while((s = strtok(string, delim_str)) != NULL && pos < buflen){
        string = NULL;
        switch(bufelem_size) {
        case 1:
            ((uint8_t*)buffer)[pos++] = (uint8_t)strtol(s, NULL, 0); break;
        case 2:
            ((int*)buffer)[pos++] = (int)strtol(s, NULL, 0); break;
        }
    }
    return pos;
}


int main(int argc, char* argv[])
{

	wchar_t wstr[MAX_STR];
    uint16_t vid = 0;        // productId
    uint16_t pid = 0;        // vendorId
    char* shortopts = "vh?";
    bool done = false;
    int option_index = 0, opt, res;

	struct option longoptions[] =
	{
		{"help", no_argument, 0, 'h'},
		{"read-one-tag", no_argument, &cmd,   		CMD_ONEPASS_READ},
		{"version",      no_argument, &cmd,         CMD_VERSION},
		{"timeout",      required_argument, &cmd,   CMD_TIME_OUT},
		{"vidpid",       required_argument, &cmd,   CMD_VIDPID},
		{"UID20bits",	 no_argument, &cmd,			CMD_UID20BITS},
		{NULL,0,0,0}
	};

    setbuf(stdout, NULL);  // turn off buffering of stdout

	while (!done) {
        opt = getopt_long(argc, argv, shortopts, longoptions, &option_index);
        if (opt==-1) done = true; // parsed all the args
        switch(opt) {
        case 0:                   // long opts with no short opts

            if( cmd == CMD_VIDPID ) {

                if( sscanf(optarg, "%4hx/%4hx", &vid,&pid) !=2 ) {  // match "23FE/AB12"
                    if( !sscanf(optarg, "%4hx:%4hx", &vid,&pid) ) { // match "23FE:AB12"
                        // else try parsing standard dec/hex values
                        int wordbuf[4]; // a little extra space
                        str2buf(wordbuf, ":/, ", optarg, sizeof(wordbuf), 2);
                        vid = wordbuf[0]; pid = wordbuf[1];
                    }
                }
                msginfo("Looking for vid/pid 0x%04X / 0x%04X  (%d / %d)\n",vid,pid,vid,pid);
            }
            else if( cmd == CMD_ONEPASS_READ ) {

                msg("Doing one pass read, ");
				ONE_PASS_READ=true;
            }
            else if( cmd == CMD_TIME_OUT ) {
				timeout_wait_seconds = strtol(optarg,NULL,10);
                msg(" %d sec timeout...",timeout_wait_seconds);
            }
            else if( cmd == CMD_VERSION ) {
                printf("lectorhid version: %s\n", LECTORHID_VERSION);
                printf("hidapi version: %d.%d.%d\n",
                       HID_API_VERSION_MAJOR, HID_API_VERSION_MINOR, HID_API_VERSION_PATCH);
            }
            else if( cmd == CMD_UID20BITS ) {
				UID_20_BITS=true;
                printf("Using 20 bits for UID, usually needed with 32 bit cards.\n");
            }
            break; // case 0 (longopts without shortops)
        case 'h':
		case '?':
            print_usage("lectorhid");
			exit(0);
            break;
       } // switch(opt)

	}	// while not done

	// Initialize buffer, should be all 0's
	for (int i=0;i<65;i++) buf[i]=0;

	// Initialize the hidapi library
	res = hid_init();

	// Open the device using the VID, PID,
	// and optionally the Serial number.
	//handle = hid_open(0x4d8, 0x3f, NULL);
	handle=Open_PCPROX(vid,pid);
	if (!handle) {
		printf("Unable to open device. Did you connect the reader to the PC?\n");
		hid_exit();
 		return 1;
	}

	// Read the Manufacturer String
	res = hid_get_manufacturer_string(handle, wstr, MAX_STR);
	printf("\nManufacturer : %ls ", wstr);

	// Read the Product String
	res = hid_get_product_string(handle, wstr, MAX_STR);
	printf("Product : %ls ", wstr);

	// Read the Serial Number String
	res = hid_get_serial_number_string(handle, wstr, MAX_STR);
	printf("Serial Number String: (%d) %ls\n", wstr[0], wstr);

	// Read Indexed String 1
	res = hid_get_indexed_string(handle, 1, wstr, MAX_STR);
	printf("Indexed String 1: %ls\n", wstr);

	// Toggle LED (cmd 0x80). The first byte is the report number (0x0).
	//buf[0] = 0x0;
	//buf[1] = 0x80;
	//res = hid_write(handle, buf, 65);

	// Request state (cmd 0x81). The first byte is the report number (0x0).
	//buf[0] = 0x0;
	//buf[1] = 0x81;
	//res = hid_write(handle, buf, 65);

	// Read requested state
	// res = hid_read(handle, buf, 65);
#ifdef PCPROX_DEBUG
	// Print out the returned buffer.
	for (int i = 0; i < 4; i++)
		printf("buf[%d]: %d\n", i, buf[i]);
#endif
	// read tag
	/*
	        # Must send 8F first, else 8E will never be set!
        card_data = self.interact(b'\x8f')
        if card_data is None:
            return None
	*/

	printf("Waiting for RFID Card to be read. ");
	if (!ONE_PASS_READ)
		printf("Press any key to exit.");
	printf("\n");
	// loop for continuos read
	time_t	time_read_start=time(NULL);

	do {
		int readUID;
		time_t	run_time;

		readUID=PCPROX_READTAGS();
		if (readUID && ONE_PASS_READ)	
			break;	// We did a good read. Exit while loop.
		run_time=time(NULL);
		if ((ONE_PASS_READ) && ((run_time-time_read_start)>timeout_wait_seconds))
			break;	// Exit if we reach timeout with one pass read
		usleep(500000); 	// wait 500ms to see if someone appears	
	} while (!kbhit());

	if (kbhit()) getch();	// Don't leave your key behind.	
	// Close the device
	hid_close(handle);

	// Finalize the hidapi library
	res = hid_exit();

	return res;
}