/*******************************************************************************
*   NEM Wallet
*   (c) 2017 Ledger
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
********************************************************************************/
#include "os.h"
#include "cx.h"
#include "base32.h"
#include <ctype.h>
#include <inttypes.h>
#include "nemHelpers.h"
#define MAX_SAFE_INTEGER 9007199254740991

static const uint8_t AMOUNT_MAX_SIZE = 17;

uint8_t readNetworkIdFromBip32path(uint32_t bip32Path[]) {
    uint8_t outNetworkId;
    switch(bip32Path[2]) {
        case 0x80000068: 
            outNetworkId = 104; //N
            break;
        case 0x80000098:
           outNetworkId = 152; //T
           break;
        case 0x80000060:
            outNetworkId = 96; //M
            break;
        case 0x80000090:
            outNetworkId = 144; //S
            break;
        default:
            THROW(0x6a80);
    }
    return outNetworkId;
}

//todo nonprintable ch + utf8
void uint2Ascii(uint8_t *inBytes, uint8_t len, char *out){
    char *tmpCh = (char *)inBytes;
    for (uint8_t j=0; j<len; j++){
        out[j] = tmpCh[j];
    }
    out[len] = '\0';
}

uint8_t *reverseBytes(uint8_t *sourceArray, uint16_t len){
    uint8_t outArray[len];
    for (uint8_t j=0; j<len; j++) {
        outArray[j] = sourceArray[len - j -1];
    }
    return outArray;
}

void print_amount(uint64_t amount, uint8_t divisibility, char *asset, char *out) {
    char buffer[AMOUNT_MAX_SIZE];
    uint64_t dVal = amount;
    int i, j;

    // If the amount can't be represented safely in JavaScript, signal an error
    //if (MAX_SAFE_INTEGER < amount) THROW(0x6a80);

    memset(buffer, 0, AMOUNT_MAX_SIZE);
    for (i = 0; dVal > 0 || i < 7; i++) {
        if (dVal > 0) {
            buffer[i] = (dVal % 10) + '0';
            dVal /= 10;
        } else {
            buffer[i] = '0';
        }
        if (i == divisibility - 1) { // divisibility
            i += 1;
            buffer[i] = '.';
            if (dVal == 0) {
                i += 1;
                buffer[i] = '0'; 
            }           
        }
        if (i >= AMOUNT_MAX_SIZE) {
            THROW(0x6700);
        }
    }
    // reverse order
    for (i -= 1, j = 0; i >= 0 && j < AMOUNT_MAX_SIZE-1; i--, j++) {
        out[j] = buffer[i];
    }
    // strip trailing 0s
    for (j -= 1; j > 0; j--) {
        if (out[j] != '0') break;
    }
    j += 1;

    // strip trailing .
    if (out[j-1] == '.') j -= 1;

    if (asset) {
        // qualify amount
        out[j++] = ' ';
        strcpy(out + j, asset);
        out[j+strlen(asset)] = '\0';
    } else {
        out[j] = '\0';
    }

}

uint16_t getUint16(uint8_t *buffer) {
    return ((uint16_t)buffer[1]) | ((uint16_t)buffer[0] << 8);
}

uint32_t getUint32(uint8_t *data) {
    return ((uint32_t)data[3]) | ((uint32_t)data[2] << 8) | ((uint32_t)data[1] << 16) |
             ((uint32_t)data[0] << 24);
}

uint64_t getUint64(uint8_t *data) {
    return ((uint64_t)data[7]) | ((uint64_t)data[6] << 8) | ((uint64_t)data[5] << 16) |
             ((uint64_t)data[4] << 24) | ((uint64_t)data[3] << 32) | ((uint64_t)data[2] << 40) |
             ((uint64_t)data[1] << 48) | ((uint64_t)data[0] << 56);
}

void to_nem_public_key_and_address(cx_ecfp_public_key_t *inPublicKey, uint8_t inNetworkId, unsigned int inAlgo, uint8_t *outNemPublicKey, unsigned char *outNemAddress) {
    uint8_t i;
    for (i=0; i<32; i++) {
        outNemPublicKey[i] = inPublicKey->W[64 - i];
    }

    if ((inPublicKey->W[32] & 1) != 0) {
        outNemPublicKey[31] |= 0x80;
    }    

    cx_sha3_t hash1;
    cx_sha3_t temphash;
    
    if (inAlgo == CX_SHA3) {
        cx_sha3_init(&hash1, 256);
        cx_sha3_init(&temphash, 256);
    }else{ //CX_KECCAK
        cx_keccak_init(&hash1, 256);
        cx_keccak_init(&temphash, 256);
    }
    unsigned char buffer1[32];
    cx_hash(&hash1.header, CX_LAST, outNemPublicKey, 32, buffer1);
    unsigned char buffer2[20];
    cx_ripemd160_t hash2;
    cx_ripemd160_init(&hash2);
    cx_hash(&hash2.header, CX_LAST, buffer1, 32, buffer2);
    unsigned char rawAddress[50];
    //step1: add network prefix char
    rawAddress[0] = inNetworkId;   //104,,,,,
    //step2: add ripemd160 hash
    os_memmove(rawAddress + 1, buffer2, sizeof(buffer2));
    
    unsigned char buffer3[32];
    cx_hash(&temphash.header, CX_LAST, rawAddress, 21, buffer3);
    //step3: add checksum
    os_memmove(rawAddress + 21, buffer3, 4);
    base32_encode(rawAddress, sizeof(rawAddress), outNemAddress, 40);
}

void public_key_to_address(uint8_t inNetworkId, uint8_t *inNemPublicKey, unsigned char *outNemAddress) {
    cx_sha3_t hash1;
    cx_sha3_t temphash;
    cx_keccak_init(&hash1, 256);
    cx_keccak_init(&temphash, 256);

    unsigned char buffer1[32];
    cx_hash(&hash1.header, CX_LAST, inNemPublicKey, 32, buffer1);
    unsigned char buffer2[20];
    cx_ripemd160_t hash2;
    cx_ripemd160_init(&hash2);
    cx_hash(&hash2.header, CX_LAST, buffer1, 32, buffer2);
    unsigned char rawAddress[50];
    //step1: add network prefix char
    rawAddress[0] = inNetworkId;   //104,,,,,
    //step2: add ripemd160 hash
    os_memmove(rawAddress + 1, buffer2, sizeof(buffer2));
    
    unsigned char buffer3[32];
    cx_hash(&temphash.header, CX_LAST, rawAddress, 21, buffer3);
    //step3: add checksum
    os_memmove(rawAddress + 21, buffer3, 4);
    base32_encode(rawAddress, sizeof(rawAddress), outNemAddress, 40);
}


unsigned int get_apdu_buffer_length() {
	unsigned int len0 = G_io_apdu_buffer[APDU_BODY_LENGTH_OFFSET];
	return len0;
}

void clean_raw_tx(unsigned char *raw_tx) {
    uint16_t i;
    for (i = 0; i < MAX_TX_RAW_LENGTH; i++) {
        raw_tx[i] = 0;
    }
}

int compare_strings (char str1[], char str2[]) {
    int index = 0;
 
    while (str1[index] == str2[index]) {
        if (str1[index] == '\0' || str2[index] == '\0')
            break;
        index++;
    }
    
    if (str1[index] == '\0' && str2[index] == '\0')
        return 0;
    else
        return -1;
}

int string_length(char str[]) {
    int index = 0;
 
    while (str[index] != '\0') {
        str++;
    }
 
    return index;
}

/** Convert 1 hex byte to 2 characters */
char hex2Ascii(uint8_t input){
    return input > 9 ? (char)(input + 87) : (char)(input + 48);
}

/** Convert hex string to character string 
    outLen = inLen*2 + 1 */
void hex2String(uint8_t *inBytes, uint8_t inLen, char *out) {
    uint8_t index;

    for (index = 0; index < inLen; index++) {
        out[2*index] = hex2Ascii((inBytes[index] & 0xf0) >> 4);
        out[2*index + 1] = hex2Ascii(inBytes[index] & 0x0f);
    }
    out[2*inLen] = '\0';
}

void parse_transfer_tx (unsigned char raw_tx[],
    unsigned int* ux_step_count, 
    char detailName[MAX_PRINT_DETAIL_NAME_SCREEN][MAX_PRINT_DETAIL_NAME_LENGTH],
    char extraInfo[MAX_PRINT_EXTRA_INFO_SCREEN][MAX_PRINT_EXTRA_INFOR_LENGTH],
    char fullAddress[NEM_ADDRESS_LENGTH],
    bool isMultisig) {
    
    //Fee
    uint64_t fee;

    //msg
    uint16_t lengthOfMessFeildIndex;
    uint32_t lengthOfMessFeild;
    uint16_t msgSizeIndex;
    uint32_t msgSize;
    uint16_t msgTypeIndex;
    uint16_t msgIndex;
    uint32_t msgType;
    char msg[MAX_PRINT_MESSAGE_LENGTH + 1];

    //mosaics
    uint16_t numberOfMosaicsIndex;
    uint8_t numberOfMosaics; 
    uint16_t mosaicIndex;

    //amount
    uint16_t amountIndex;
    uint32_t amount; 

    //Namespace ID
    uint16_t lengthOfIDIndex;
    uint32_t lengthOfID;
    uint16_t IDNameIndex;

    //Mosaic Name
    uint16_t lengthOfNameIndex;
    uint32_t lengthOfName;
    uint16_t nameIndex;
    char IDName[MAX_PRINT_EXTRA_INFOR_LENGTH];
    char name[MAX_PRINT_EXTRA_INFOR_LENGTH];

    //Supply type
    uint8_t supplyType; 

    //Quantity
    uint16_t quantityIndex;
    uint32_t quantity;

    //Array index
    uint8_t arrayIndex; 

    *ux_step_count = 5;

    //Address
    SPRINTF(detailName[0], "%s", "Recipient");
    uint2Ascii(&raw_tx[4+4+4+4+32+4+4+4+4], 40, fullAddress); 

    //Message
    SPRINTF(detailName[1], "%s", "Message");
    lengthOfMessFeildIndex = 4+4+4+4+32+4+4+4+4+40+4+4;
    lengthOfMessFeild = getUint32(reverseBytes(&raw_tx[lengthOfMessFeildIndex], 4));
    msgSizeIndex = lengthOfMessFeild == 0 ? 0 : lengthOfMessFeildIndex+4+4;
    msgSize = lengthOfMessFeild == 0 ? 0 : getUint32(reverseBytes(&raw_tx[msgSizeIndex], 4));
    msgTypeIndex = lengthOfMessFeildIndex+4;
    msgIndex = lengthOfMessFeildIndex+4+4+4;
    msgType = getUint32(reverseBytes(&raw_tx[msgTypeIndex], 4));
    if (lengthOfMessFeild == 0) {
        SPRINTF(extraInfo[0], "%s\0", "<empty msg>");
    }
    else if(msgType == 1) {
        if (raw_tx[msgIndex] == 0xFE) {
            SPRINTF(detailName[1], "%s", "Hex message");
            msgIndex++;
            for (arrayIndex = 0; (arrayIndex < msgSize - 1) && (arrayIndex*2 < 12); arrayIndex++) {
                msg[2*arrayIndex] = hex2Ascii((raw_tx[msgIndex + arrayIndex] & 0xf0) >> 4);
                msg[2*arrayIndex + 1] = hex2Ascii(raw_tx[msgIndex + arrayIndex] & 0x0f);
                msg[2*arrayIndex + 2] = '\0';
            }
            if ( arrayIndex*2 + 1> MAX_PRINT_MESSAGE_LENGTH) {
                SPRINTF(extraInfo[0], "%s ...\0", msg);
            } else {
                SPRINTF(extraInfo[0], "%s\0", msg);
            }
        } else if (msgSize > MAX_PRINT_MESSAGE_LENGTH) {
            uint2Ascii(&raw_tx[msgIndex], MAX_PRINT_MESSAGE_LENGTH, msg);
            SPRINTF(extraInfo[0], "%s ...\0", msg);
        } else {
            uint2Ascii(&raw_tx[msgIndex], msgSize, msg);
            SPRINTF(extraInfo[0], "%s", msg);
        }
    } else {
        SPRINTF(extraInfo[0], "%s\0", "<encrypted msg>");
    }

    //Fee
    SPRINTF(detailName[2], "%s", "Fee");
    fee = getUint32(reverseBytes(&raw_tx[4+4+4+4+32], 4));
    if (isMultisig) {
        fee += 150000;
    }
    print_amount((uint64_t *)fee, 6, "xem", &extraInfo[1]);

    //mosaics
    numberOfMosaicsIndex = lengthOfMessFeild == 0 ? lengthOfMessFeildIndex+4: lengthOfMessFeildIndex+4+4+4+msgSize;
    numberOfMosaics = getUint32(reverseBytes(&raw_tx[numberOfMosaicsIndex], 4));
    mosaicIndex = numberOfMosaicsIndex+4;
    
    //amount
    SPRINTF(detailName[3], "%s", "Amount");
    if (numberOfMosaics == 0) {
        amountIndex = 4+4+4+4+32+4+4+4+4+40;
        amount = getUint32(reverseBytes(&raw_tx[amountIndex], 4));
        print_amount((uint64_t *)amount, 6, "xem", &extraInfo[2]);
    } else {
        SPRINTF(extraInfo[2], "<find %d mosaics>", numberOfMosaics);
        
        //Show all mosaics on Ledger
        for (arrayIndex = 0; arrayIndex < numberOfMosaics; arrayIndex++) {
            //Namespace ID
            lengthOfIDIndex = mosaicIndex+4+4;
            lengthOfID = getUint32(reverseBytes(&raw_tx[lengthOfIDIndex], 4));
            IDNameIndex = mosaicIndex+4+4+4;
            mosaicIndex = IDNameIndex + lengthOfID;
            uint2Ascii(&raw_tx[IDNameIndex], lengthOfID, IDName);

            //Mosaic Name
            lengthOfNameIndex = mosaicIndex;
            lengthOfName = getUint32(reverseBytes(&raw_tx[lengthOfNameIndex], 4));
            nameIndex = lengthOfNameIndex+4;
            mosaicIndex = nameIndex + lengthOfName;
            uint2Ascii(&raw_tx[nameIndex], lengthOfName, name);

            //Quantity
            quantity = getUint32(reverseBytes(&raw_tx[mosaicIndex], 4));
            *ux_step_count = *ux_step_count + 1;
            if ((compare_strings(IDName,"nem") == 0) && (compare_strings(name,"xem") == 0)) {
                SPRINTF(detailName[4 + arrayIndex], "%s %d", "Amount", 1 + arrayIndex);
                print_amount((uint64_t *)quantity, 6, "xem", extraInfo[3 + arrayIndex]);
            } else {
                SPRINTF(detailName[4 + arrayIndex], "%s %d", "Raw units", 1 + arrayIndex);
                if (string_length(name) < 13) {
                    SPRINTF(extraInfo[3 + arrayIndex], "%d %s", quantity, name);
                } else {
                    SPRINTF(extraInfo[3 + arrayIndex], "%d %s...", quantity, name);
                }
            }
            mosaicIndex += 8;
        }
    }
}

void parse_mosaic_definition_tx (unsigned char raw_tx[],
    unsigned int* ux_step_count, 
    char detailName[MAX_PRINT_DETAIL_NAME_SCREEN][MAX_PRINT_DETAIL_NAME_LENGTH],
    char extraInfo[MAX_PRINT_EXTRA_INFO_SCREEN][MAX_PRINT_EXTRA_INFOR_LENGTH],
    char fullAddress[NEM_ADDRESS_LENGTH],
    bool isMultisig) {

    //Fee
    uint64_t fee;

    //msg
    uint16_t msgSizeIndex;
    uint32_t msgSize;
    uint16_t msgIndex;
    char msg[MAX_PRINT_MESSAGE_LENGTH + 1];

    //amount
    uint16_t amountIndex;
    uint32_t amount; 

    //Namespace ID
    uint16_t lengthOfIDIndex;
    uint32_t lengthOfID;
    uint16_t IDNameIndex;

    //Mosaic Name
    uint16_t lengthOfNameIndex;
    uint32_t lengthOfName;
    uint16_t nameIndex;
    char IDName[MAX_PRINT_EXTRA_INFOR_LENGTH];
    char name[MAX_PRINT_EXTRA_INFOR_LENGTH];

    //Requires Levy
    uint16_t levySizeIndex;
    uint32_t levySize;
    uint16_t insideLevyIndex;

    *ux_step_count = 11;

    //Namespace ID
    SPRINTF(detailName[0], "%s", "Namespace");
    lengthOfIDIndex = 16+32+16+32+4+4;
    lengthOfID = getUint32(reverseBytes(&raw_tx[lengthOfIDIndex], 4));
    IDNameIndex= lengthOfIDIndex+4;
    uint2Ascii(&raw_tx[IDNameIndex], lengthOfID, fullAddress);

    //Mosaic Name
    SPRINTF(detailName[1], "%s", "Mosaic Name");
    lengthOfNameIndex = IDNameIndex + lengthOfID;
    lengthOfName = getUint32(reverseBytes(&raw_tx[lengthOfNameIndex], 4));
    nameIndex = lengthOfNameIndex+4;
    uint2Ascii(&raw_tx[nameIndex], lengthOfName, name);
    SPRINTF(extraInfo[0], "%s", name);

    //Fee
    SPRINTF(detailName[2], "%s", "Fee");
    fee = getUint32(reverseBytes(&raw_tx[4+4+4+4+32], 4));
    if (isMultisig) {
        fee += 150000;
    }
    print_amount((uint64_t *)fee, 6, "xem", &extraInfo[1]);
    
    //Description
    SPRINTF(detailName[4], "%s", "Description");
    msgSizeIndex = nameIndex+lengthOfName;
    msgSize = getUint32(reverseBytes(&raw_tx[msgSizeIndex], 4));
    msgIndex = msgSizeIndex+4;
    if(msgSize > MAX_PRINT_MESSAGE_LENGTH){
        uint2Ascii(&raw_tx[msgIndex], MAX_PRINT_MESSAGE_LENGTH, msg);
        SPRINTF(extraInfo[3], "%s...\0", msg);
    } else {
        uint2Ascii(&raw_tx[msgIndex], msgSize, msg);
        SPRINTF(extraInfo[3], "%s\0", msg);
    }

    //Start Properties
    //divisibility
    SPRINTF(detailName[6], "%s", "Divisibility");
    msgIndex = msgIndex + msgSize + 4+4+4+12+4;
    uint2Ascii(&raw_tx[msgIndex], 1, msg);
    SPRINTF(extraInfo[5], "%s", msg);

    //initial Supply
    SPRINTF(detailName[5], "%s", "Initial Supply");
    msgSizeIndex = msgIndex+1 + 4+4+13;
    msgSize = getUint32(reverseBytes(&raw_tx[msgSizeIndex], 4));
    msgIndex = msgSizeIndex + 4;
    uint2Ascii(&raw_tx[msgIndex], msgSize, msg);
    SPRINTF(extraInfo[4], "%s", msg);

    //Transferable
    SPRINTF(detailName[7], "%s", "Mutable Supply");
    msgSizeIndex = msgIndex+msgSize + 4+4+13;
    msgSize = getUint32(reverseBytes(&raw_tx[msgSizeIndex], 4));
    msgIndex = msgSizeIndex + 4;
    uint2Ascii(&raw_tx[msgIndex], msgSize, msg);
    SPRINTF(extraInfo[6], "%s", compare_strings(msg, "true") == 0 ? "Yes" : "No");

    //Mutable Supply
    SPRINTF(detailName[8], "%s", "Transferable");
    msgSizeIndex = msgIndex+msgSize + 4+4+12;
    msgSize = getUint32(reverseBytes(&raw_tx[msgSizeIndex], 4));
    msgIndex = msgSizeIndex + 4;
    uint2Ascii(&raw_tx[msgIndex], msgSize, msg);
    SPRINTF(extraInfo[7], "%s", compare_strings(msg, "true") == 0 ? "Yes" : "No");

    //Requires Levy
    SPRINTF(detailName[9], "%s", "Requires Levy");
    levySizeIndex = msgIndex+msgSize;
    levySize = getUint32(reverseBytes(&raw_tx[levySizeIndex], 4));
    SPRINTF(extraInfo[8], "%s", levySize == 0 ? "No" : "Yes");

    //Rental Fee
    SPRINTF(detailName[3], "%s", "Rental Fee");
    amountIndex = levySizeIndex+levySize + 4+4+40;
    amount = getUint32(reverseBytes(&raw_tx[amountIndex], 4));
    print_amount((uint64_t *)amount, 6, "xem", extraInfo[2]);
    //End Properties
}

void parse_mosaic_supply_change_tx (unsigned char raw_tx[],
    unsigned int* ux_step_count, 
    char detailName[MAX_PRINT_DETAIL_NAME_SCREEN][MAX_PRINT_DETAIL_NAME_LENGTH],
    char extraInfo[MAX_PRINT_EXTRA_INFO_SCREEN][MAX_PRINT_EXTRA_INFOR_LENGTH],
    char fullAddress[NEM_ADDRESS_LENGTH],
    bool isMultisig) {

    //Fee
    uint64_t fee;

    //Namespace ID
    uint16_t lengthOfIDIndex;
    uint32_t lengthOfID;
    uint16_t IDNameIndex;

    //Mosaic Name
    uint16_t lengthOfNameIndex;
    uint32_t lengthOfName;
    uint16_t nameIndex;
    char fullAddressIDName[MAX_PRINT_EXTRA_INFOR_LENGTH];
    char name[MAX_PRINT_EXTRA_INFOR_LENGTH];

    //Supply type
    uint8_t supplyType; 

    //Quantity
    uint16_t quantityIndex;
    uint32_t quantity;
    
    *ux_step_count = 5;

    //Namespace ID
    SPRINTF(detailName[0], "%s", "Namespace");
    lengthOfIDIndex = 16+32+12+4;
    lengthOfID = getUint32(reverseBytes(&raw_tx[lengthOfIDIndex], 4));
    IDNameIndex= lengthOfIDIndex+4;
    uint2Ascii(&raw_tx[IDNameIndex], lengthOfID, fullAddress);

    //Mosaic Name
    SPRINTF(detailName[1], "%s", "Mosaic Name");
    lengthOfNameIndex = IDNameIndex + lengthOfID;
    lengthOfName = getUint32(reverseBytes(&raw_tx[lengthOfNameIndex], 4));
    nameIndex = lengthOfNameIndex+4;
    uint2Ascii(&raw_tx[nameIndex], lengthOfName, name);
    SPRINTF(extraInfo[0], "%s", name);

    //Fee
    SPRINTF(detailName[2], "%s", "Fee");
    fee = getUint32(reverseBytes(&raw_tx[4+4+4+4+32], 4));
    if (isMultisig) {
        fee += 150000;
    }
    print_amount((uint64_t *)fee, 6, "xem", &extraInfo[1]);

    //Supply type
    supplyType = getUint32(reverseBytes(&raw_tx[nameIndex+lengthOfName], 4));
    quantity = getUint32(reverseBytes(&raw_tx[nameIndex+lengthOfName+4], 4));
    if (supplyType == 0x01) {   //Increase supply
        SPRINTF(detailName[3], "%s", "Increase");
    } else { //Decrease supply 
        SPRINTF(detailName[3], "%s", "Decrease");
    }
    SPRINTF(extraInfo[2], "%d", quantity);
}

void parse_provision_namespace_tx (unsigned char raw_tx[],
    unsigned int* ux_step_count, 
    char detailName[MAX_PRINT_DETAIL_NAME_SCREEN][MAX_PRINT_DETAIL_NAME_LENGTH],
    char extraInfo[MAX_PRINT_EXTRA_INFO_SCREEN][MAX_PRINT_EXTRA_INFOR_LENGTH],
    char fullAddress[NEM_ADDRESS_LENGTH],
    bool isMultisig) {

    //Fee
    uint64_t fee;

    //msg
    uint16_t msgSizeIndex;
    uint32_t msgSize;
    uint16_t msgIndex;
    char msg[MAX_PRINT_MESSAGE_LENGTH + 1];

    //Quantity
    uint16_t quantityIndex;
    uint32_t quantity;

    *ux_step_count = 6;

    //Sink Address
    SPRINTF(detailName[0], "%s", "Sink Address");
    uint2Ascii(&raw_tx[4+4+4+4+32+4+4+4+4], 40, fullAddress); 

    //Rental Fee
    SPRINTF(detailName[1], "%s", "Rental Fee");
    quantityIndex = 4+4+4+4+32+4+4+4+4+40;
    quantity = getUint32(reverseBytes(&raw_tx[quantityIndex], 4));
    print_amount((uint64_t *)quantity, 6, "xem", extraInfo[0]);

    //Fee
    SPRINTF(detailName[2], "%s", "Fee");
    fee = getUint32(reverseBytes(&raw_tx[4+4+4+4+32], 4));
    if (isMultisig) {
        fee += 150000;
    }
    print_amount((uint64_t *)fee, 6, "xem", &extraInfo[1]);

    //Namespace
    SPRINTF(detailName[3], "%s", "Namespace");
    msgSizeIndex = quantityIndex + 8;
    msgSize = getUint32(reverseBytes(&raw_tx[msgSizeIndex], 4));
    msgIndex = msgSizeIndex + 4;
    uint2Ascii(&raw_tx[msgIndex], msgSize, msg);
    SPRINTF(extraInfo[2], "%s", msg);

    //Parent namespace
    SPRINTF(detailName[4], "%s", "Parent Name");
    msgSizeIndex = msgIndex + msgSize;
    msgSize = getUint32(reverseBytes(&raw_tx[msgSizeIndex], 4));
    if (msgSize == -1) {
        SPRINTF(extraInfo[3], "%s", "<New namespace>"); 
    } else {
        msgIndex = msgSizeIndex + 4;
        uint2Ascii(&raw_tx[msgIndex], msgSize, msg);
        SPRINTF(extraInfo[3], "%s", msg);
    }
}

void parse_aggregate_modification_tx (unsigned char raw_tx[],
    unsigned int* ux_step_count,
    char detailName[MAX_PRINT_DETAIL_NAME_SCREEN][MAX_PRINT_DETAIL_NAME_LENGTH],
    char extraInfo[MAX_PRINT_EXTRA_INFO_SCREEN][MAX_PRINT_EXTRA_INFOR_LENGTH],
    char fullAddress[NEM_ADDRESS_LENGTH],
    bool isMultisig,
    uint8_t networkId) {

    //Fee
    uint64_t fee;

    //Cosign Address
    uint16_t    numOfCosigModificationIndex;
    uint8_t     numOfCosigModification;
    uint16_t    publicKeyIndex;
    char        address[40];
    uint16_t    typeOfModificationIndex;
    uint8_t     typeOfModification;
    uint8_t     index;

    //Min signatures
    uint16_t    minSigIndex;
    int8_t      minSig;

    *ux_step_count = 3;

    //Multisig Account
    publicKeyIndex = 4+4+4+4;
    public_key_to_address (networkId, &raw_tx[publicKeyIndex], address);
    if (isMultisig) { //Affected
        SPRINTF(detailName[0], "%s", "Edited account");
    } else { //Converted
        SPRINTF(detailName[0], "%s", "Converted Acc");
    }
    os_memset(fullAddress, 0, sizeof(fullAddress));                
    os_memmove((void *)fullAddress, address, 40);

    //Cosignatures
    numOfCosigModificationIndex = 4+4+4+4+32+8+4;
    numOfCosigModification = getUint32(reverseBytes(&raw_tx[numOfCosigModificationIndex], 4));
    typeOfModificationIndex = numOfCosigModificationIndex + 4;

    for (index = 0; index < numOfCosigModification; index++) {
        *ux_step_count = *ux_step_count + 1;
        typeOfModificationIndex += 4;
        typeOfModification = getUint32(reverseBytes(&raw_tx[typeOfModificationIndex], 4));

        publicKeyIndex = typeOfModificationIndex +4+4;
        public_key_to_address (networkId, &raw_tx[publicKeyIndex], address);

        //Top line
        if (typeOfModification == 0x01) {
            SPRINTF(detailName[index+1], "%s", "Add cosign");
        } else {
            SPRINTF(detailName[index+1], "%s", "Remove cosign");
        }
        //Bottom line
        os_memset(extraInfo[index], 0, sizeof(extraInfo[index]));                
        os_memmove((void *)extraInfo[index], address, 6);
        os_memmove((void *)(extraInfo[index] + 6), "~", 1);
        os_memmove((void *)(extraInfo[index] + 6 + 1), address + 40 - 4, 4);

        typeOfModificationIndex = typeOfModificationIndex + 4 + 4 + 32;
        numOfCosigModificationIndex = typeOfModificationIndex;
    }

    //Min signatures
    minSigIndex = numOfCosigModification == 0 ? numOfCosigModificationIndex + 4+4 : numOfCosigModificationIndex +4;
    minSig = getUint32(reverseBytes(&raw_tx[minSigIndex], 4));
    if (minSig > 0) {
        SPRINTF(detailName[numOfCosigModification+1], "%s", "Num of minsig");
        SPRINTF(extraInfo[numOfCosigModification], "Increase %d", minSig);
    } else if (minSig < 0) {
        SPRINTF(detailName[numOfCosigModification+1], "%s", "Num of minsig");
        SPRINTF(extraInfo[numOfCosigModification], "Decrease %d", ~minSig + 1);
    }
    if (minSig != 0) {
        numOfCosigModification += 1;
        *ux_step_count = *ux_step_count + 1;
    }

    //Fee
    SPRINTF(detailName[numOfCosigModification+1], "%s", "Fee");
    fee = getUint32(reverseBytes(&raw_tx[4+4+4+4+32], 4));
    if (isMultisig) {
        fee += 150000;
    }
    print_amount((uint64_t *)fee, 6, "xem", &extraInfo[numOfCosigModification]);
}

void parse_multisig_tx (unsigned char raw_tx[],
    unsigned int* ux_step_count, 
    char detailName[MAX_PRINT_DETAIL_NAME_SCREEN][MAX_PRINT_DETAIL_NAME_LENGTH],
    char extraInfo[MAX_PRINT_EXTRA_INFO_SCREEN][MAX_PRINT_EXTRA_INFOR_LENGTH],
    char fullAddress[NEM_ADDRESS_LENGTH],
    uint8_t networkId) {
    
    uint32_t otherTxType = getUint32(reverseBytes(&raw_tx[0], 4));

    switch (otherTxType) {
        case NEMV1_TRANSFER:
            parse_transfer_tx (raw_tx,
                ux_step_count, 
                detailName,
                extraInfo,
                fullAddress,
                true
            );
            break;
        case NEMV1_PROVISION_NAMESPACE:
            parse_provision_namespace_tx (raw_tx,
                ux_step_count, 
                detailName,
                extraInfo,
                fullAddress,
                true
            );
            break;
        case NEMV1_MOSAIC_DEFINITION:
            parse_mosaic_definition_tx (raw_tx,
                ux_step_count, 
                detailName,
                extraInfo,
                fullAddress,
                true
            );
            break;
        case NEMV1_MOSAIC_SUPPLY_CHANGE:
            parse_mosaic_supply_change_tx (raw_tx,
                ux_step_count, 
                detailName,
                extraInfo,
                fullAddress,
                true
            );
            break;
        case NEMV1_MULTISIG_MODIFICATION:
            parse_aggregate_modification_tx (raw_tx,
                ux_step_count, 
                detailName,
                extraInfo,
                fullAddress,
                true,
                networkId
            );
            break;
        default:
            break;
    }
}

void parse_multisig_signature_tx (unsigned char raw_tx[],
    unsigned int* ux_step_count,
    char detailName[MAX_PRINT_DETAIL_NAME_SCREEN][MAX_PRINT_DETAIL_NAME_LENGTH],
    char extraInfo[MAX_PRINT_EXTRA_INFO_SCREEN][MAX_PRINT_EXTRA_INFOR_LENGTH],
    char fullAddress[NEM_ADDRESS_LENGTH]) {

    //Fee
    uint8_t multisigFeeIndex;
    uint64_t fee;

    //multisig Address
    uint8_t multisigAddressIndex;

    //Hash bytes
    uint8_t hashBytesIndex;
    char hashBytes[65];

    uint8_t index;

    *ux_step_count = 4;
    
    //Cosign transaction for
    SPRINTF(detailName[0], "%s", "Cosign tx for");
    multisigAddressIndex = 4+4+4+4+32+8+4+4+4+32+4;
    uint2Ascii(&raw_tx[multisigAddressIndex], 40, fullAddress);

    //Hash
    SPRINTF(detailName[1], "%s", "SHA hash");
    hashBytesIndex = 4+4+4+4+32+8+4+ 4+4;
    for (index = 0; index < 32; index++) {
        hashBytes[2*index] = hex2Ascii((raw_tx[index + hashBytesIndex] & 0xf0) >> 4);
        hashBytes[2*index + 1] = hex2Ascii(raw_tx[index + hashBytesIndex] & 0x0f);
    }
    os_memset(extraInfo[0], 0, sizeof(extraInfo[0]));                
    os_memmove((void *)extraInfo[0], hashBytes, 6);
    os_memmove((void *)(extraInfo[0] + 6), "~", 1);
    os_memmove((void *)(extraInfo[0] + 6 + 1), hashBytes + 64 - 4, 4);

    //Multisig fee
    SPRINTF(detailName[2], "%s", "Multisig fee");
    multisigFeeIndex = 4+4+4+4+32;
    fee = getUint32(reverseBytes(&raw_tx[multisigFeeIndex], 4));
    print_amount((uint64_t *)fee, 6, "xem", &extraInfo[1]);
}

//Catapult
void parse_catapult_transfer_tx (
    unsigned char raw_tx[],
    unsigned int* ux_step_count,
    char detailName[MAX_PRINT_DETAIL_NAME_SCREEN][MAX_PRINT_DETAIL_NAME_LENGTH],
    char extraInfo[MAX_PRINT_EXTRA_INFO_SCREEN][MAX_PRINT_EXTRA_INFOR_LENGTH],
    char extraInfo_0[NEM_ADDRESS_LENGTH],
    bool isMultisig) {

    //Recipient Address
    uint16_t recipientAddressIndex;
    uint8_t tmpAddress[41];

    //Message
    uint16_t msgSizeIndex;
    uint16_t msgSize;
    uint16_t msgIndex;
    char msg[MAX_PRINT_MESSAGE_LENGTH + 1];

    //Fee
    uint64_t fee;

    //Mosaics
    uint16_t numMosaicIndex;
    uint8_t numMosaic;
    uint16_t offset;
    uint32_t lowMosaicId;
    uint32_t highMosaicId;
    uint64_t amount;
    uint8_t index;

    *ux_step_count = 5;

    //Recipient Address
    SPRINTF(detailName[0], "%s", "Recipient");
    recipientAddressIndex = isMultisig ? 2+2 :2+2+8+8;
    base32_encode(&raw_tx[recipientAddressIndex], 25, &tmpAddress, 40);
    tmpAddress[40] = '\0';
    os_memset(extraInfo_0, 0, sizeof(extraInfo_0));
    os_memmove((void *)extraInfo_0, tmpAddress, 41);

    //Fee
    if (!isMultisig) {
        fee = getUint64(reverseBytes(&raw_tx[2+2], 8));
        SPRINTF(detailName[1], "%s", "Fee");
        print_amount(fee, 6, "xem", &extraInfo[0]);
    }

    //Message
    SPRINTF(detailName[2], "%s", "Message");
    msgSizeIndex = isMultisig ? 2+2+25: 2+2+8+8+25;
    msgSize = getUint16(reverseBytes(&raw_tx[msgSizeIndex], 2));
    PRINTF("msg size: %d\n", msgSize);
    if (msgSize <= 1) {
        SPRINTF(extraInfo[1], "%s\0", "<empty msg>");
    } else {
        msgIndex = isMultisig ? 2+2+25+2+1+1 : 2+2+8+8+25+2+1+1;
        if (msgSize > MAX_PRINT_MESSAGE_LENGTH) {
            uint2Ascii(&raw_tx[msgIndex], MAX_PRINT_MESSAGE_LENGTH, msg);
            SPRINTF(extraInfo[1], "%s...\0", msg);
        } else {
            uint2Ascii(&raw_tx[msgIndex], msgSize, msg);
            SPRINTF(extraInfo[1], "%s\0", msg);
        }
    }

    //Mosaic
    SPRINTF(detailName[3], "%s", "Mosaic");
    numMosaicIndex = isMultisig ? 2+2+25+2: 2+2+8+8+25+2;
    numMosaic = raw_tx[numMosaicIndex];
    SPRINTF(extraInfo[2], "<find %d mosaics>", numMosaic);

    offset  = isMultisig ? 32 :48;
    offset += msgSize;

    for (index = 0; index < numMosaic; index++) {
        *ux_step_count = *ux_step_count + 1;
        //Mosaic ID
        lowMosaicId = getUint32(reverseBytes(&raw_tx[offset], 4));
        highMosaicId = getUint32(reverseBytes(&raw_tx[offset+4], 4));
        //Quantity
        offset +=8;
        amount = getUint64(reverseBytes(&raw_tx[offset], 8));
        offset +=8;

        if ((highMosaicId == 0x77a19699) && (lowMosaicId == 0x32d987d7)) {
            //nem.xem
            SPRINTF(detailName[4+index], "Nem");
            print_amount(amount, 6, "xem", &extraInfo[3+index]); // mosaicDivisibility = 6
        } else {
            //unkown mosaic
            SPRINTF(extraInfo[3+index], "%x%x", highMosaicId, lowMosaicId);
            print_amount(amount, 0, "", &detailName[4+index]); // mosaicDivisibility = 0
        }
    }
}

void parse_catapult_provision_namespace_tx (
    unsigned char raw_tx[],
    unsigned int* ux_step_count,
    char detailName[MAX_PRINT_DETAIL_NAME_SCREEN][MAX_PRINT_DETAIL_NAME_LENGTH],
    char extraInfo[MAX_PRINT_EXTRA_INFO_SCREEN][MAX_PRINT_EXTRA_INFOR_LENGTH],
    char extraInfo_0[NEM_ADDRESS_LENGTH],
    bool isMultisig) {

    //Fee
    uint16_t feeIndex;
    uint64_t fee;

    //Type
    uint16_t registrationTypeIndex;
    uint8_t registrationType;

    //Duration
    uint16_t blockDurationIndex;
    uint64_t blockDuration;
    uint8_t day;
    uint8_t hour;
    uint8_t min;

    //Parent Namespace
    uint16_t namespaceIdIndex;
    uint64_t namespaceId;

    //Name
    uint16_t nameSizeIndex;
    uint8_t nameSize;

    *ux_step_count = 4;

    //Type; 0: Root namespace, 1: Child namespace
    registrationTypeIndex = 4+8+8;
    registrationType = raw_tx[registrationTypeIndex];

    if (registrationType == 1) {
        //Id, Parent namespace identifier is required for subnamespaces.
        SPRINTF(detailName[2], "%s", "Parent ID");
        namespaceIdIndex = 21+8;
        uint32_t lowParentId = getUint32(reverseBytes(&raw_tx[namespaceIdIndex], 4));
        uint32_t highParentId = getUint32(reverseBytes(&raw_tx[namespaceIdIndex+4], 4));
        SPRINTF(extraInfo[1], "%x%x", highParentId, lowParentId);
    } else {
        //Duration
        SPRINTF(detailName[2], "%s", "Duration");
        blockDurationIndex = 20+1;
        blockDuration = getUint64(reverseBytes(&raw_tx[blockDurationIndex], 8));
        day = blockDuration / 7200;
        hour = (blockDuration % 7200) / 300;
        min = (blockDuration % 300) / 5;
        SPRINTF(extraInfo[1], "%d%s%d%s%d%s", day, "d ", hour, "h ", min, "m");
    }
    
    //Name
    SPRINTF(detailName[0], "%s", "Name");
    nameSizeIndex = 37;
    nameSize = raw_tx[nameSizeIndex];
    uint2Ascii(&raw_tx[nameSizeIndex+1], nameSize, extraInfo_0);

    //Fee
    SPRINTF(detailName[1], "%s", "Fee");
    feeIndex = 4;
    fee = getUint64(reverseBytes(&raw_tx[feeIndex], 8));
    print_amount(fee, 6, "xem", &extraInfo[0]);
}

void parse_catapult_mosaic_definition_tx (
    unsigned char raw_tx[],
    unsigned int* ux_step_count, 
    char detailName[MAX_PRINT_DETAIL_NAME_SCREEN][MAX_PRINT_DETAIL_NAME_LENGTH],
    char extraInfo[MAX_PRINT_EXTRA_INFO_SCREEN][MAX_PRINT_EXTRA_INFOR_LENGTH],
    char extraInfo_0[NEM_ADDRESS_LENGTH],
    bool isMultisig) {

    //Supply amount
    uint16_t supplyAmountIndex;
    uint64_t supplyAmount;

    //Divisibility
    uint16_t divisibilityIndex;
    uint8_t divisibility;

    //Duration
    uint16_t blockDurationIndex;
    uint64_t blockDuration;
    uint8_t day;
    uint8_t hour;
    uint8_t min;

    //MosaicFlags
    uint16_t mosaicFlagsIndex;
    uint8_t mosaicFlags;

    *ux_step_count = 8;

    //Supply amount
    SPRINTF(detailName[0], "%s", "Supply amount");
    supplyAmountIndex = 62 + 49;
    supplyAmount = getUint64(reverseBytes(&raw_tx[supplyAmountIndex], 8));
    os_memset(extraInfo_0, 0, sizeof(extraInfo_0));
    print_amount(supplyAmount*10, 1, "\0", extraInfo_0);

    //Divisibility
    SPRINTF(detailName[1], "%s", "Divisibility");
    divisibilityIndex = 4+32+17;
    divisibility = raw_tx[divisibilityIndex];
    SPRINTF(extraInfo[0], "%d", divisibility);

    //Duration
    SPRINTF(detailName[2], "%s", "Duration");
    blockDurationIndex = 4+32+18;
    blockDuration = getUint64(reverseBytes(&raw_tx[blockDurationIndex], 8));
    if (blockDuration <= 0) {
        SPRINTF(extraInfo[1], "%s", "Unlimited");
    } else {
        day = blockDuration / 7200;
        hour = (blockDuration % 7200) / 300;
        min = (blockDuration % 300) / 5;
        SPRINTF(extraInfo[1], "%d%s%d%s%d%s", day, "d ", hour, "h ", min, "m");
    }

    //Mosaic Flags
    mosaicFlagsIndex = 4+32+16;
    mosaicFlags = raw_tx[mosaicFlagsIndex];

    //Transmittable
    SPRINTF(detailName[4], "%s", "Transmittable");
    if (mosaicFlags & 0x02 ) {
        SPRINTF(extraInfo[3], "%s", "Yes");
    } else {
        SPRINTF(extraInfo[3], "%s", "No");
    }

    //Supply multable
    SPRINTF(detailName[5], "%s", "SupplyMultable");
    if (mosaicFlags & 0x01) {
        SPRINTF(extraInfo[4], "%s", "Yes");
    } else {
        SPRINTF(extraInfo[4], "%s", "No");
    }

    //Restrictable
    SPRINTF(detailName[6], "%s", "Restrictable");
    if (mosaicFlags & 0x04) {
        SPRINTF(extraInfo[5], "%s", "Yes");
    } else {
        SPRINTF(extraInfo[5], "%s", "No");
    }
}

void parse_catapult_aggregate_complete_tx (
    unsigned char raw_tx[],
    unsigned int* ux_step_count, 
    char txTypeName[30],
    char detailName[MAX_PRINT_DETAIL_NAME_SCREEN][MAX_PRINT_DETAIL_NAME_LENGTH],
    char extraInfo[MAX_PRINT_EXTRA_INFO_SCREEN][MAX_PRINT_EXTRA_INFOR_LENGTH],
    char extraInfo_0[NEM_ADDRESS_LENGTH],
    bool isMultisig) {

    //Fee
    uint16_t feeIndex = 4;
    uint64_t fee;

    *ux_step_count = 1;

    uint16_t txType = getUint16(reverseBytes(&raw_tx[60 + 2], 2));
    os_memset(txTypeName, 0, sizeof(txTypeName));

    fee = getUint64(reverseBytes(&raw_tx[feeIndex], 8));

    switch(txType){
        case TRANSFER: //Transfer 
            os_memmove((void *)txTypeName, "Multisig TX", 12);

            //Fee
            SPRINTF(detailName[1], "%s", "Fee");
            print_amount(fee, 6, "xem", &extraInfo[0]);

            parse_catapult_transfer_tx (
                raw_tx + 60,
                ux_step_count, 
                detailName,
                extraInfo,
                extraInfo_0,
                true
            ); 
            break;
        case MOSAIC_DEFINITION:
            os_memmove((void *)txTypeName, "Create Mosaic", 14);

            //Fee
            SPRINTF(detailName[3], "%s", "Fee");
            print_amount(fee, 6, "xem", &extraInfo[2]);

            parse_catapult_mosaic_definition_tx (
                raw_tx + 24,
                ux_step_count,
                detailName,
                extraInfo,
                extraInfo_0,
                false);
            break;
        default:
            os_memmove((void *)txTypeName, "test", 5);
            break;
    }
}

void parse_catapult_aggregate_bonded_tx (
    unsigned char raw_tx[],
    unsigned int* ux_step_count, 
    char txTypeName[30],
    char detailName[MAX_PRINT_DETAIL_NAME_SCREEN][MAX_PRINT_DETAIL_NAME_LENGTH],
    char extraInfo[MAX_PRINT_EXTRA_INFO_SCREEN][MAX_PRINT_EXTRA_INFOR_LENGTH],
    char extraInfo_0[NEM_ADDRESS_LENGTH],
    bool isMultisig
) {
    *ux_step_count = 1;

    uint16_t txType = getUint16(reverseBytes(&raw_tx[60 + 2], 2));
    os_memset(txTypeName, 0, sizeof(txTypeName));

    switch(txType){
        case MODIFY_MULTISIG_ACCOUNT:
            os_memmove((void *)txTypeName, "Multisig TX", 11);
            parse_catapult_multisig_account_modification_tx (
                raw_tx + 60,
                ux_step_count, 
                detailName,
                extraInfo,
                extraInfo_0,
                false);
            break;
        default:
            break; 
    }
}

void parse_catapult_multisig_account_modification_tx (
    unsigned char raw_tx[],
    unsigned int* ux_step_count, 
    char detailName[MAX_PRINT_DETAIL_NAME_SCREEN][MAX_PRINT_DETAIL_NAME_LENGTH],
    char extraInfo[MAX_PRINT_EXTRA_INFO_SCREEN][MAX_PRINT_EXTRA_INFOR_LENGTH],
    char extraInfo_0[NEM_ADDRESS_LENGTH],
    bool isMultisig
) {
    //Create or modify a multisig contract.
    //min Removal Delta
    uint16_t minRemovalDeltaIndex;
    uint8_t minRemovalDelta;

    //min Approval Delta
    uint16_t minApprovalDeltaIndex;
    uint8_t minApprovalDelta;

    //modification
    uint16_t modificationsIndex;
    uint16_t modificationTypeIndex;
    uint8_t modificationsCount;
    uint8_t modificationType;
    uint8_t index;

    *ux_step_count = 3;

    //min Removal Delta
    SPRINTF(detailName[0], "%s", "Min Removal");
    minRemovalDeltaIndex = 4;
    minRemovalDelta = raw_tx[minRemovalDeltaIndex];
    os_memset(extraInfo_0, 0, sizeof(extraInfo_0));
    print_amount(minRemovalDelta*10, 1, "\0", extraInfo_0);

    //min Approval Delta
    SPRINTF(detailName[1], "%s", "Min Approval");
    minApprovalDeltaIndex = 5;
    minApprovalDelta = raw_tx[minApprovalDeltaIndex];
    SPRINTF(extraInfo[0], "%d", minApprovalDelta);

    //modification
    modificationsIndex = 6;
    modificationsCount = raw_tx[modificationsIndex];
    modificationTypeIndex = 7;
    modificationsIndex = 8;
    char publicKey[65]; 
    
    for (index = 0; index < modificationsCount; index++) {
        *ux_step_count = *ux_step_count + 1;

        modificationType = raw_tx[modificationTypeIndex];

        //Top line
        if (modificationType == 0x01) {
            SPRINTF(detailName[index + 2], "%s", "Add cosign");
        } else {
            SPRINTF(detailName[index + 2], "%s", "Remove cosign");
        }
        //Bottom line
        hex2String(&raw_tx[modificationsIndex], 32, publicKey);
        os_memset(extraInfo[index + 1], 0, sizeof(extraInfo[index + 1]));
        os_memmove((void *)extraInfo[index + 1], publicKey, 6);
        os_memmove((void *)(extraInfo[index + 1] + 6), "~", 1);
        os_memmove((void *)(extraInfo[index + 1] + 6 + 1), publicKey + 64 - 4, 4);

        modificationTypeIndex += 1 + 32;
        modificationsIndex = modificationTypeIndex + 1;
    }

}