#include "patcher.h"
#include "config.h"
#include "network.h"
#include "nand.h"
#include "network/picohttpparser.h"
#include <malloc.h>


unsigned int calcChecksum(char* buffer, int length) {
    int totalChecksum = 0;
    for (int i = 0; i < length; i += 4) {
        int currentBytes;
        memcpy(&currentBytes, buffer + i, 4);

        totalChecksum += currentBytes;
    }

    return totalChecksum;
}

s64 getFriendCode() {
    // Open the file containing the friend code
    static char buffer[32];
    s32 error = NAND_ReadFile("/shared2/wc24/nwc24msg.cfg", buffer, 32);
    if (error < 0) return error;

    // Copy the friend code (0x8 -> 0xF)
    s64 fc = 0;
    memcpy(&fc, buffer + 0x8, 0x8);

    return fc;
}

void patchNWC24MSG(unionNWC24MSG* unionFile, char passwd[0x20], char mlchkid[0x24]) {
    // Patch mail domain
    strcpy(unionFile->structNWC24MSG.mailDomain, BASE_MAIL_URL);

    // Patch mlchkid and passwd
    strcpy(unionFile->structNWC24MSG.passwd, passwd);
    strcpy(unionFile->structNWC24MSG.mlchkid, mlchkid);

    // Patch the URLs
    const char *engines[] = { "account", "check", "receive", "delete", "send" };
    for (int i = 0; i < 5; i++) {
        memset(unionFile->structNWC24MSG.urls[i], 0, sizeof(unionFile->structNWC24MSG.urls[i]));
        sprintf(unionFile->structNWC24MSG.urls[i], "http://%s/cgi-bin/%s.cgi", BASE_HTTP_URL, engines[i]);
    }

    // Patch the title booting
    unionFile->structNWC24MSG.titleBooting = 1;

    // Update the checksum
    int checksum = calcChecksum(unionFile->charNWC24MSG, 0x3FC);
    unionFile->structNWC24MSG.checksum = checksum;
}

s32 patchMail() {
    // Read the nwc24msg.cfg file
    unionNWC24MSG fileUnionNWC24MSG;
    s32 error = NAND_ReadFile("/shared2/wc24/nwc24msg.cfg", fileUnionNWC24MSG.charNWC24MSG, sizeof(fileUnionNWC24MSG));
    if (error < 0) {
        printf(":-----------------------------------------:\n");
        printf(": The nwc24msg.cfg file couldn't be read. :\n");
        printf(":-----------------------------------------:\n\n");
        return error;
    }

    // Separate the file magic and checksum
    unsigned int oldChecksum = fileUnionNWC24MSG.structNWC24MSG.checksum;
    unsigned int calculatedChecksum = calcChecksum(fileUnionNWC24MSG.charNWC24MSG, 0x3FC);

    // Check the file magic and checksum
    if (memcmp(fileUnionNWC24MSG.structNWC24MSG.magic, "WcCf", 4) != 0) {
        printf(":-----------------------------------------:\n");
        printf(": Invalid nwc24msg.cfg file.              :\n");
        printf(":-----------------------------------------:\n\n");
        return -1;
    }
    if (oldChecksum != calculatedChecksum) {
        printf(":-----------------------------------------:\n");
        printf(": The checksum isn't corresponding.       :\n");
        printf(":-----------------------------------------:\n\n");
        return -1;
    }

    // Get the friend code
    s64 fc = fileUnionNWC24MSG.structNWC24MSG.friendCode;
    if (fc < 0) {
        printf(":-----------------------------------------:\n");
        printf(" Invalid Friend Code: %lli\n", fc);
        printf(":-----------------------------------------:\n\n");
        return fc;
    }


    // Request for a passwd/mlchkid
    char arg[30];
    sprintf(arg, "mlid=w%016lli", fc);
    char url[128];
    sprintf(url, "https://%s/cgi-bin/patcher.cgi", BASE_PATCH_URL);
    char *response = (char *)malloc(2048);
    error = post_request(url, arg, &response);

	if (error > 0) {
		printf(":-------------------------------------------------------------:\n");
		printf(": Failed to send request to the server.                       :\n");
		printf(": Please check if your Wii is connected to the Internet.      :\n");
		printf(":                                                             :\n");
		printf(": %-60s"                                                     ":\n", cURL_GetLastError());
		printf(":-------------------------------------------------------------:\n\n");

        return error;
	}
	else if (error < 0) {
		return error;
	}

    // Parse the response
    struct phr_header headers[10];
    size_t num_headers;
    num_headers = sizeof(headers) / sizeof(headers[0]);
    error = phr_parse_headers(response, strlen(response) + 1, headers, &num_headers, 0);

    serverResponseCode responseCode = RESPONSE_NOTINIT;
    char responseMlchkid[0x24] = "";
    char responsePasswd[0x20] = "";

    for (int i = 0; i != num_headers; ++i) {
        char* currentHeaderName;
        currentHeaderName = malloc((int)headers[i].name_len);
        sprintf(currentHeaderName, "%.*s", (int)headers[i].name_len, headers[i].name);

        char* currentHeaderValue;
        currentHeaderValue = malloc((int)headers[i].value_len);
        sprintf(currentHeaderValue, "%.*s", (int)headers[i].value_len, headers[i].value);

        if (strcmp(currentHeaderName, "cd") == 0)
            responseCode = atoi(currentHeaderValue);
        else if (strcmp(currentHeaderName, "mlchkid") == 0)
            memcpy(&responseMlchkid, currentHeaderValue, 0x24);
        else if (strcmp(currentHeaderName, "passwd") == 0)
            memcpy(&responsePasswd, currentHeaderValue, 0x20);
    }

    // Check the response code
    switch (responseCode) {
    case RESPONSE_INVALID:
		printf(":-----------------------------------------:\n");
		printf(": Invalid friend code.                    :\n");
        printf(":-----------------------------------------:\n\n");
        return 1;
        break;
    case RESPONSE_AREGISTERED:
        printf(":----------------------------------------------------:\n");
        printf(": Your Wii's Friend Code is already in our database. :\n");
        printf(":----------------------------------------------------:\n\n");
        return RESPONSE_AREGISTERED;
        break;
    case RESPONSE_DB_ERROR:
        printf(":-----------------------------------------:\n");
        printf(": Server database error. Try again later. :\n");
        printf(":-----------------------------------------:\n");
        return 1;
        break;
    case RESPONSE_OK:
        if (strcmp(responseMlchkid, "") == 0 || strcmp(responsePasswd, "") == 0) {
            // If it's empty, nothing we can do.
            return 1;
        } else {
            // Patch the nwc24msg.cfg file
            printf("Domain before: %s\n", fileUnionNWC24MSG.structNWC24MSG.mailDomain);
            patchNWC24MSG(&fileUnionNWC24MSG, responsePasswd, responseMlchkid);
            printf("Domain after: %s\n\n", fileUnionNWC24MSG.structNWC24MSG.mailDomain);

            error = NAND_WriteFile("/shared2/wc24/nwc24msg.cfg", fileUnionNWC24MSG.charNWC24MSG, 0x400, false);
            if (error < 0) {
			printf(":--------------------------------------------:\n");
			printf(": The nwc24msg.cfg file couldn't be updated. :\n");
			printf(":--------------------------------------------:\n\n");
                return error;
            }
            return 0;
            break;
        }
    default:
        printf("Incomplete data. Check if the server is up.\nFeel free to send a developer the "
               "following content: \n%s\n",
               response);
        return 1;
        break;
    }

    return 0;
}
