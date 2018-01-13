#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "jpeglib.h"
#include "jconfig.h"

typedef unsigned char BYTE;
typedef unsigned int DWORD;
typedef unsigned int LONG;
typedef unsigned short WORD;

typedef enum picFormat
{
    PIC_BMP,
    PIC_JPEG,
    PIC_RGB,
}PICFORMAT;

typedef struct picInfo
{
    BYTE *pBuffer;
    unsigned bufferLen;
    unsigned width;
    unsigned height;
    unsigned short depth;
    PICFORMAT format;
}PICTUREINFO;

typedef struct tagBITMAPFILEHEADER
{ 
    WORD bfType;        //2Bytes，必须为"BM"，即0x424D 才是Windows位图文件
    DWORD bfSize;         //4Bytes，整个BMP文件的大小
    WORD bfReserved1;  //2Bytes，保留，为0
    WORD bfReserved2;  //2Bytes，保留，为0
    DWORD bfOffBits;     //4Bytes，文件起始位置到图像像素数据的字节偏移量
} BITMAPFILEHEADER;

typedef struct _tagBMP_INFOHEADER
{
    DWORD  biSize;    //4Bytes，INFOHEADER结构体大小，存在其他版本I NFOHEADER，用作区分
    LONG   biWidth;    //4Bytes，图像宽度（以像素为单位）
    LONG   biHeight;    //4Bytes，图像高度，+：图像存储顺序为Bottom2Top，-：Top2Bottom
    WORD   biPlanes;    //2Bytes，图像数据平面，BMP存储RGB数据，因此总为1
    WORD   biBitCount;         //2Bytes，图像像素位数
    DWORD  biCompression;     //4Bytes，0：不压缩，1：RLE8，2：RLE4
    DWORD  biSizeImage;       //4Bytes，4字节对齐的图像数据大小
    LONG   biXPelsPerMeter;   //4 Bytes，用象素/米表示的水平分辨率
    LONG   biYPelsPerMeter;   //4 Bytes，用象素/米表示的垂直分辨率
    DWORD  biClrUsed;          //4 Bytes，实际使用的调色板索引数，0：使用所有的调色板索引
    DWORD biClrImportant;     //4 Bytes，重要的调色板索引数，0：所有的调色板索引都重要
}BMP_INFOHEADER;

static void _jpgfile_to_jpgmem(char *jpg_file,BYTE **jpg,int *size)
{
    FILE *fp = fopen(jpg_file,"rb");
    if(fp == NULL) return;

    fseek(fp,0,SEEK_END);
    int length = ftell(fp);
    fseek(fp,0,SEEK_SET);

    *jpg = new BYTE[length];
    fread(*jpg,length,1,fp);
    *size = length;

    fclose(fp);
}

static bool _jpgToRGBColor(PICTUREINFO picInputInfo, PICTUREINFO *picOutputInfo)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    BYTE *inBuffer = picInputInfo.pBuffer;
    unsigned inBufferLen = picInputInfo.bufferLen;
    BYTE *outBuffer = NULL;

    if(picInputInfo.pBuffer == NULL || picInputInfo.bufferLen<=0 || picInputInfo.format != PIC_JPEG)
    {
        printf("%s %d error input parameters!\n", __FUNCTION__, __LINE__);
        return false;
    }

    if(!picOutputInfo)
    {
        printf("%s %d error input parameters!\n", __FUNCTION__, __LINE__);
        return false;
    }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, inBuffer, inBufferLen);
    
    jpeg_read_header(&cinfo, TRUE);
    if(!jpeg_start_decompress(&cinfo))
    {
        printf("%s %d decode failed!\n", __FUNCTION__, __LINE__);

        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    unsigned picWidth = cinfo.output_width;
    unsigned picHeight = cinfo.output_height;
    unsigned picDepth = cinfo.output_components;

    printf("The picture width is:%u, height is:%u, depth is:%u\n", picWidth, picHeight, picDepth);

    outBuffer = (BYTE *)malloc(picWidth*picHeight*picDepth);
    if(!(outBuffer))
    {
        printf("%s %d allocate buffer failed!\n", __FUNCTION__, __LINE__);
        return false;
    }
    memset(outBuffer, 0x00, picWidth*picHeight*picDepth);

    JSAMPARRAY buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, picWidth*picHeight,1);
    BYTE *pTmpBuffer = (outBuffer)+(picHeight-cinfo.output_scanline-1)*(picWidth*picDepth);
    while(cinfo.output_scanline<picHeight)
    {
        jpeg_read_scanlines(&cinfo, buffer, 1);
        memcpy(pTmpBuffer, *buffer, picWidth*picDepth);
        pTmpBuffer -= picWidth*picDepth;
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    picOutputInfo->pBuffer = outBuffer;
    picOutputInfo->bufferLen = picWidth*picHeight*picDepth;
    picOutputInfo->depth = picDepth;
    picOutputInfo->format = PIC_RGB;
    picOutputInfo->width = picWidth;
    picOutputInfo->height = picHeight;
   
    return true;
}

#define FOUR_BYTES_ALIGN(width_in_bit)  ((((width_in_bit)+31)>>5)<<2)  


static BYTE _bmpHeader[54] =
{
    0x42,0x4D, //bmp type
    0x00,0x00,0x00,0x00, //bmp file size;
    0x00,0x00, //reserve0
    0x00,0x00, //reserve1
    0x00,0x00,0x00,0x00, //RGB data offset

    0x00,0x00,0x00,0x00, //header info size
    0x00,0x00,0x00,0x00, //picture height
    0x00,0x00,0x00,0x00, //picture width
    0x01,0x00,   //the number of plane
    0x18,0x00,   //the color bit counts
    0x00,0x00,0x00,0x00, //the compression type
    0x00,0x00,0x00,0x00, //the size of picture
    0x00,0x00,0x00,0x00, //the horizontal resolution
    0x00,0x00,0x00,0x00, //the vertical resolution
    0x00,0x00,0x00,0x00, //biClrUsed
    0x00,0x00,0x00,0x00, //biClrImportant
};

#if 1
static bool _BGR2BmpFile(FILE *pBmpFile, PICTUREINFO RGBInfo)
{
    unsigned alignWidth = FOUR_BYTES_ALIGN((RGBInfo.width)*24);
    unsigned realWidth = (RGBInfo.width)*3;
    BYTE zeroBuffer[4] = {0x0};
    BYTE *buffer = NULL;

    if(pBmpFile == NULL)
    {
        printf("%s %d parameter error!\n", __FUNCTION__, __LINE__);
        return false;
    }

    //bmp file size;
    DWORD bmpFileSize = 0x36+(RGBInfo.height)*alignWidth;
    _bmpHeader[2] = bmpFileSize&0xff;
    _bmpHeader[3] = (bmpFileSize&0xff00)>>8;
    _bmpHeader[4] = (bmpFileSize&0xff0000)>>16;
    _bmpHeader[5] = (bmpFileSize&0xff0000)>>24;

    //the RGB data offset;
    _bmpHeader[10] = 0x36;
    //the size of BMP_INFOHEADER;
    _bmpHeader[14] = 0x28;

    //the picture height;
    _bmpHeader[18] = (RGBInfo.width)&0xff;
    _bmpHeader[19] = ((RGBInfo.width)&0xff00)>>8;
    _bmpHeader[20] = ((RGBInfo.width)&0xff0000)>>16;
    _bmpHeader[21] = ((RGBInfo.width)&0xff000000)>>24;

    //the picture width;
    _bmpHeader[22] = (RGBInfo.height)&0xff;
    _bmpHeader[23] = ((RGBInfo.height)&0xff00)>>8;
    _bmpHeader[24] = ((RGBInfo.height)&0xff0000)>>16;
    _bmpHeader[25] = ((RGBInfo.height)&0xff000000)>>24;

    fwrite((const void*)_bmpHeader, 0x36, 1, pBmpFile);

    if(realWidth<alignWidth)
    {
        buffer = (BYTE *)malloc(alignWidth);
        memset(buffer, 0x0, alignWidth);
    }
    while((RGBInfo.height)--)
    {
        if(realWidth<alignWidth)
        {
            memcpy(buffer, RGBInfo.pBuffer, realWidth);
            fwrite(buffer, alignWidth, 1, pBmpFile);
        }
        else
        {
            fwrite(RGBInfo.pBuffer, realWidth, 1, pBmpFile);
        }

        RGBInfo.pBuffer += realWidth;
    }

    if(buffer)
    {
        free(buffer);
    }

    return true;
}
#else
static bool _BGR2BmpFile(FILE *pBmpFile, PICTUREINFO RGBInfo)
{
    BITMAPFILEHEADER bmpHeader;
    BMP_INFOHEADER bmpInfo;
    unsigned alignWidth = FOUR_BYTES_ALIGN((RGBInfo.width)*24);
    unsigned realWidth = (RGBInfo.width)*3;
    BYTE zeroBuffer[4] = {0x0};

    if(pBmpFile == NULL)
    {
        printf("%s %d parameter error!\n", __FUNCTION__, __LINE__);
        return false;
    }

    memset((void*)&bmpHeader, 0x00, sizeof(BITMAPFILEHEADER));
    bmpHeader.bfType = 0x4D42;
    bmpHeader.bfSize = 0x36+(RGBInfo.height)*alignWidth;
    bmpHeader.bfOffBits = 0x36;
  //  printf("%s %d BITMAPFILEHEADER:%u!\n", __FUNCTION__, __LINE__, sizeof(BITMAPFILEHEADER));

    memset((void*)&bmpInfo, 0x00, sizeof(BMP_INFOHEADER));
    bmpInfo.biBitCount = 24;
    bmpInfo.biSize = 0x28;
    bmpInfo.biWidth = RGBInfo.width;
    bmpInfo.biHeight = RGBInfo.height;
    bmpInfo.biPlanes = 1;
    bmpInfo.biCompression = 0;
    bmpInfo.biSizeImage = 0;
    bmpInfo.biXPelsPerMeter = 0;
    bmpInfo.biYPelsPerMeter = 0;
  //  printf("%s %d BITMAPFILEHEADER:%u!\n", __FUNCTION__, __LINE__, sizeof(BMP_INFOHEADER));

    fwrite(&bmpHeader, 2, 1, pBmpFile);
    fwrite(((char*)(&bmpHeader)+4), 12, 1, pBmpFile);
    fwrite(&bmpInfo, sizeof(BMP_INFOHEADER), 1, pBmpFile);

    while((RGBInfo.height)--)
    {
        fwrite(RGBInfo.pBuffer, realWidth, 1, pBmpFile);
        if(realWidth<alignWidth)
        {
            fwrite(zeroBuffer, alignWidth-realWidth, 1, pBmpFile);
        }

        RGBInfo.pBuffer += realWidth;
    }
    
    return true;
}
#endif

static void _RGB2BGR(PICTUREINFO picInfo)
{
    unsigned char* pRGBData = picInfo.pBuffer;
    unsigned bufferLen = picInfo.bufferLen;

    unsigned char tmpData;

    while(bufferLen)
    {
        memcpy(&tmpData, pRGBData, 1);
        memcpy(pRGBData, pRGBData+2, 1);
        memcpy(pRGBData+2, &tmpData, 1);
    
        pRGBData +=3;
        bufferLen -=3;
    }

    return;
}


int main(int argc, char* argv[])
{
    char fileName[1024];
    FILE *pInput = NULL;
    struct _stat fileInfo;
    BYTE * tmpBuffer = NULL;

    if(argc != 3)
    {
        printf("%s %d parameter error!\n", __FUNCTION__, __LINE__);
        return -1;
    }

    printf("Input file path:%s\n", argv[1]);
    strncpy((char*) fileName, (const char*)argv[1], 1023);

    if(_stat(fileName, &fileInfo)<0)
    {
        printf("stat file:%s failed!\n", fileName);
        return -1;
    }
    tmpBuffer = (BYTE *)malloc(fileInfo.st_size+0x100);
    if(!tmpBuffer)
    {
        printf("allocate buffer failed!\n");
        return -1;
    }

    pInput = fopen((const char*)fileName, "rb");
    if(pInput == NULL)
    {
        printf("Open file failed!\n");
        return -1;
    }
    fread((void*)tmpBuffer, fileInfo.st_size, 1, pInput);

    //jpeg decode
    PICTUREINFO inPicInfo;
    PICTUREINFO outPicInfo;
    inPicInfo.pBuffer = tmpBuffer;
    inPicInfo.bufferLen = fileInfo.st_size;
    inPicInfo.format = PIC_JPEG;
    if(!_jpgToRGBColor(inPicInfo, &outPicInfo))
    {
        printf("%s %d decode jpeg failed!\n", __FUNCTION__, __LINE__);
        return -1;
    }

    //convert RGB data to BRR data
    _RGB2BGR(outPicInfo);

    //write the BGR data to bmp file;
    FILE* pBMPFile = NULL;
    memset((void*)fileName, 0x00, 1024);
    strncpy((char*)fileName, (const char*)argv[2], 1023);
    if((pBMPFile=fopen((const char*)fileName, "wb+")) == NULL)
    {
        printf("%s %d fopen file failed!\n", __FUNCTION__, __LINE__);
        return -1;
    }
    _BGR2BmpFile(pBMPFile, outPicInfo);
    fclose(pBMPFile);


    printf("unsigned:%d, unsigned short:%d, unsigned char:%d\n",sizeof(unsigned int), sizeof(unsigned short), sizeof(unsigned char));

    fclose(pInput);
    free(outPicInfo.pBuffer);
    free(tmpBuffer);
    return 0;
}