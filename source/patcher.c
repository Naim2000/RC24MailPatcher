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
    const char engines[0x5][0x80] = { "account", "check", "receive", "delete", "send" };
    for (int i = 0; i < 5; i++) {
        char formattedLink[0x80];
        for (int i = 0; i < sizeof(formattedLink); i++)
        {
            formattedLink[i] = '\0';
        }
        strncpy(unionFile->structNWC24MSG.urls[i], formattedLink, sizeof(formattedLink));
    }
    for (int i = 0; i < 5; i++) {
        char formattedLink[0x80];
        sprintf(formattedLink, "http://%s/cgi-bin/%s.cgi", BASE_HTTP_URL, engines[i]);
        strcpy(unionFile->structNWC24MSG.urls[i], formattedLink);
    }

    // Patch the title booting
    unionFile->structNWC24MSG.titleBooting = 1;

    // Update the checksum
    int checksum = calcChecksum(unionFile->charNWC24MSG, 0x3FC);
    unionFile->structNWC24MSG.checksum = checksum;
}

s32 patchMail() {
    // Read the nwc24msg.cfg file
    static char fileBufferNWC24MSG[0x400];
    unionNWC24MSG fileUnionNWC24MSG;

    s32 error = NAND_ReadFile("/shared2/wc24/nwc24msg.cfg", fileBufferNWC24MSG, 0x400);
    if (error < 0) {
        printf(":-----------------------------------------:\n");
        printf(": The nwc24msg.cfg file couldn't be read. :\n");
        printf(":-----------------------------------------:\n\n");
        return error;
    }
    memcpy(&fileUnionNWC24MSG, fileBufferNWC24MSG, 0x400);

    // Separate the file magic and checksum
    unsigned int oldChecksum = fileUnionNWC24MSG.structNWC24MSG.checksum;
    unsigned int calculatedChecksum = calcChecksum(fileUnionNWC24MSG.charNWC24MSG, 0x3FC);

    // Check the file magic and checksum
    if (strcmp(fileUnionNWC24MSG.structNWC24MSG.magic, "WcCf") != 0) {
        printf("The file couldn't be verified\n");
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

    // cURL error code 6 could also mean the host doesn't exists, but we know for a fact it does.
	if (error == 6) {
		printf(":--------------------------------------------------------:");
		printf("\n: There is no Internet connection.                       :");
		printf("\n: Please check if your Wii is connected to the Internet. :");
		printf("\n:                                                        :");
		printf("\n: Could not reach remote host.                           :");
		printf("\n:--------------------------------------------------------:\n\n");
		
	}
	if (error < 0) {
		return error;
	}
	if (error < 0 && error != 6) {
        printf(":-----------------------------------------:\n");
        printf(" Couldn't request the data: %i\n", error);
        printf(":-----------------------------------------:\n\n");
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
