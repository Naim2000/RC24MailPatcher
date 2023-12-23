#include <gccore.h>
#include <wiiuse/wpad.h>
#include <wiisocket.h>
#include <unistd.h>

#include "nand.h"
#include "patcher.h"

static void* xfb = NULL;
static GXRModeObj* rmode = NULL;

char version_major = 1;
char version_minor = 4;
char version_patch = 0;

// sorry had to make this a subroutine
static void printContactInfo(s64 friendCode) {
    printf(
        "Contact info:\n"
        "- Discord: https://discord.gg/rc24\n"
        "		Wait time: Short, send a Direct Message to a developer.\n"
        "- E-Mail: support@riiconnect24.net\n"
        "		Wait time: up to 24 hours, sometimes longer\n\n"
    );

    if (friendCode)
        printf(
            "When contacting, please provide a brief explanation of the issue, and\n"
            "include your Wii Number: w%016lli\n\n", friendCode);
}

int main(void) {
    VIDEO_Init();

    rmode = VIDEO_GetPreferredMode(NULL);
    xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

	printf("\n:---------------------------------------------------------:\n");
	printf("  RiiConnect24 Mail Patcher - (C) RiiConnect24 ");
#ifdef COMMITHASH
    printf("v%u.%u.%u-%s\n", version_major, version_minor, version_patch, COMMITHASH);
#else
    printf("v%u.%u.%u\n", version_major, version_minor, version_patch);
#endif
    printf("  Compiled on %s at %s\n", __DATE__ , __TIME__);
	printf(":---------------------------------------------------------:\n\n");

	printf("Initializing... ");
    WPAD_Init();
	NAND_Init();
    wiisocket_init();
    printf("OK!\n\n");
	
    if (isDolphin()) {
        printf(":---------------------------------------------------------------:\n"
               ": Dolphin is not supported!                                     :\n"
               ": This tool can only run on a real Wii Console.                 :\n"
               ":                                                               :\n"
               ": Exiting in 10 seconds...                                      :\n"
               ":---------------------------------------------------------------:\n");
        sleep(10);
        exit(0);
    }

    printf("Patching...\n\n");

    s64 friendCode = getFriendCode();
    s32 error = patchMail();

    switch (error) {
    case RESPONSE_AREGISTERED:
        printf(
                "You have already patched your Wii to use Wii Mail.\n"
                "In most cases, there is no need to run this patcher again.\n"
                "If you're having any sorts of problems, reinstalling RiiConnect24\n"
                "is unnecessary and unlikely to fix issues.\n"
                "If you still need to have your Wii Number removed, please contact us.\n\n");
        printContactInfo(friendCode);
        break;
    case 22: // You needed CURLOPT_FAILONERROR for this to actually happen
        // cURL's error code 22 covers all the HTTP error codes higher or equal to 400
        printf(
                "We're probably performing maintenance or having some issues. Hang tight!\n"
                "Make sure to check https://status.rc24.xyz/ for more info.\n\n");
        printContactInfo(0);
        break;
    case 0:
        // Success
        printf("All done, all done! Thank you for installing RiiConnect24.\n");
        break;
    default:
        printf(
            "There was an error while patching.\n"
            "Please make a screenshot of this error message and contact us.\n\n");
        printContactInfo(friendCode);
        break;
    }

    printf("\nPress the HOME Button to exit.\n");
    while (1) {
        WPAD_ScanPads();
        u32 pressed = WPAD_ButtonsDown(0);

        if (pressed & WPAD_BUTTON_HOME) break;

        VIDEO_WaitVSync();
    }

    NAND_Exit();

    return 0;
}
