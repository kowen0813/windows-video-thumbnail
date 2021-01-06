#include <iostream>
#include <string>

// Media Foundation 
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

// Shell
#include <shobjidl.h>
#include <shellapi.h>

#include <assert.h>
#include <propvarutil.h>

//#include <gdiplus.h>
//#include <gdiplusheaders.h>
#include "atlimage.h"
using namespace std;

struct FormatInfo
{
    UINT32          imageWidthPels;
    UINT32          imageHeightPels;
    BOOL            bTopDown;
    RECT            rcPicture;    // Corrected for pixel aspect ratio
    LONG            stride;

    FormatInfo() : imageWidthPels(0), imageHeightPels(0), bTopDown(FALSE)
    {
        SetRectEmpty(&rcPicture);
    }
};

template <class T> void SafeRelease(T** ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

const LONGLONG SEEK_TOLERANCE = 10000000;
const LONGLONG MAX_FRAMES_TO_SKIP = 2;
const int MAX_LONG_SIDE = 400;

FormatInfo      m_format;
IMFSourceReader* m_pReader = NULL;



//-------------------------------------------------------------------
// GetVideoFormat
// 
// Gets format information for the video stream.
//
// iStream: Stream index.
// pFormat: Receives the format information.
//-------------------------------------------------------------------

HRESULT GetVideoFormat()
{
    HRESULT hr = S_OK;
    UINT32  width = 0, height = 0;
    LONG lStride = 0;
    //MFRatio par = { 0 , 0 };
    float ratio = 1.0;

    FormatInfo& format = m_format;

    GUID subtype = { 0 };

    IMFMediaType* pType = NULL;

    // Get the media type from the stream.
    hr = m_pReader->GetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        &pType
    );

    if (FAILED(hr)) { goto done; }

    // Make sure it is a video format.
    hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (subtype != MFVideoFormat_RGB32)
    {
        hr = E_UNEXPECTED;
        goto done;
    }

    // Get the width and height
    hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);

    if (FAILED(hr)) { goto done; }

    // Get the stride to find out if the bitmap is top-down or bottom-up.
    lStride = (LONG)MFGetAttributeUINT32(pType, MF_MT_DEFAULT_STRIDE, 1);

    format.bTopDown = (lStride > 0);
    format.stride = lStride;

    if (width >= height)
    {
        if (width <= MAX_LONG_SIDE) {
            ratio = 1.0;
        }else {
            ratio = (float)width / MAX_LONG_SIDE;
        }
    }else {
        if (height <= MAX_LONG_SIDE) {
            ratio = 1.0;
        }
        else {
            ratio = (float)height / MAX_LONG_SIDE;
        }
    }
    SetRect(&format.rcPicture, 0, 0, width/ratio, height/ratio);
    format.imageWidthPels = width;
    format.imageHeightPels = height;

done:
    SafeRelease(&pType);

    return hr;
}


HRESULT SelectVideoStream()
{
    HRESULT hr = S_OK;

    IMFMediaType* pType = NULL;

    // Configure the source reader to give us progressive RGB32 frames.
    // The source reader will load the decoder if needed.

    hr = MFCreateMediaType(&pType);

    if (SUCCEEDED(hr))
    {
        hr = pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    }

    if (SUCCEEDED(hr))
    {
        hr = pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    }


    if (SUCCEEDED(hr))
    {
        hr = m_pReader->SetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            NULL,
            pType
        );
    }

    // Ensure the stream is selected.
    if (SUCCEEDED(hr))
    {
        hr = m_pReader->SetStreamSelection(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            TRUE
        );
    }

    if (SUCCEEDED(hr))
    {
        hr = GetVideoFormat();
    }

    SafeRelease(&pType);
    return hr;
}


//-------------------------------------------------------------------
// CanSeek: Queries whether the current video file is seekable.
//-------------------------------------------------------------------

HRESULT CanSeek(BOOL* pbCanSeek)
{
    HRESULT hr = S_OK;

    ULONG flags = 0;

    PROPVARIANT var;
    PropVariantInit(&var);

    if (m_pReader == NULL)
    {
        return MF_E_NOT_INITIALIZED;
    }

    *pbCanSeek = FALSE;

    hr = m_pReader->GetPresentationAttribute(
        (DWORD)MF_SOURCE_READER_MEDIASOURCE,
        MF_SOURCE_READER_MEDIASOURCE_CHARACTERISTICS,
        &var
    );

    if (SUCCEEDED(hr))
    {
        hr = PropVariantToUInt32(var, &flags);
    }

    if (SUCCEEDED(hr))
    {
        // If the source has slow seeking, we will treat it as
        // not supporting seeking. 

        if ((flags & MFMEDIASOURCE_CAN_SEEK) &&
            !(flags & MFMEDIASOURCE_HAS_SLOW_SEEK))
        {
            *pbCanSeek = TRUE;
        }
    }

    return hr;
}

HRESULT OpenFile(const WCHAR* fileName)
{
    HRESULT hr = S_OK;

    IMFAttributes* pAttributes = NULL;
    

    // Configure the source reader to perform video processing.
    //
    // This includes:
    //   - YUV to RGB-32
    //   - Software deinterlace

    hr = MFCreateAttributes(&pAttributes, 1);

    if (SUCCEEDED(hr))
    {
        hr = pAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }

    // Create the source reader from the URL.

    if (SUCCEEDED(hr))
    {
        hr = MFCreateSourceReaderFromURL(fileName, pAttributes, &m_pReader);
    }

    if (SUCCEEDED(hr))
    {
        // Attempt to find a video stream.
        SelectVideoStream();
    }

    return hr;
}

//-------------------------------------------------------------------
// GetDuration: Finds the duration of the current video file.
//-------------------------------------------------------------------

HRESULT GetDuration(LONGLONG* phnsDuration)
{
    PROPVARIANT var;
    PropVariantInit(&var);

    HRESULT hr = S_OK;

    if (m_pReader == NULL)
    {
        return MF_E_NOT_INITIALIZED;
    }

    hr = m_pReader->GetPresentationAttribute(
        (DWORD)MF_SOURCE_READER_MEDIASOURCE,
        MF_PD_DURATION,
        &var
    );

    if (SUCCEEDED(hr))
    {
        assert(var.vt == VT_UI8);
        *phnsDuration = var.hVal.QuadPart;
    }

    PropVariantClear(&var);

    return hr;
}

HRESULT save_thumb(IMFSample* pSample, LONGLONG hnsPos, UINT16 frame_num)
{
    HRESULT     hr = S_OK;

    BYTE* pBitmapData = NULL;    // Bitmap data
    DWORD       cbBitmapData = 0;       // Size of data, in bytes
    IMFMediaBuffer* pBuffer = 0;

    if (pSample)
    {
        UINT32 pitch = 4 * m_format.imageWidthPels;

        // Get the bitmap data from the sample, and use it to create a
        // Direct2D bitmap object. Then use the Direct2D bitmap to 
        // initialize the sprite.

        hr = pSample->ConvertToContiguousBuffer(&pBuffer);

        if (FAILED(hr)) { goto done; }

        hr = pBuffer->Lock(&pBitmapData, NULL, &cbBitmapData);

        if (FAILED(hr)) { goto done; }

        assert(cbBitmapData == (pitch * m_format.imageHeightPels));
        
        /*
        Gdiplus::Bitmap image(m_format.imageWidthPels, m_format.imageHeightPels, m_format.stride, PixelFormat32bppRGB, pBitmapData);
        Gdiplus::Image* im = new Gdiplus::Bitmap(m_format.rcPicture.right, m_format.rcPicture.bottom);
        Gdiplus::Graphics g(im);
        g.ScaleTransform((float)m_format.rcPicture.right/ (float)m_format.imageWidthPels, (float)m_format.rcPicture.bottom/ (float)m_format.imageHeightPels);
        g.DrawImage(&image, 0, 0);
        
        
        CLSID clsId;
        // Get the class identifier for the PNG encoder
		// *  - BMP:  557CF400-1A04-11D3-9A73-0000F81EF32E
		// *  - JPEG: 557CF401-1A04-11D3-9A73-0000F81EF32E
		// *  - GIF:  557CF402-1A04-11D3-9A73-0000F81EF32E
		// *  - PNG:  557CF406-1A04-11D3-9A73-0000F81EF32E
        ::CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &clsId);
        */
        CString  strFileName;
        strFileName.Format(_T("%I64d.jpg"), frame_num);
        cout << frame_num << " pos: " << hnsPos << " \n";
        
        
        CImage destImage, destImage2;
        HDC hDstDC = NULL;
        destImage.Create(m_format.imageWidthPels, m_format.imageHeightPels, 32, 0);
        destImage2.Create(m_format.rcPicture.right, m_format.rcPicture.bottom, 32, 0);
        BYTE* destPtr = (BYTE*)destImage.GetBits();
        int destPitch = destImage.GetPitch();
        int destBitsCount = destImage.GetBPP();
        
        for (int i = 0; i < m_format.imageHeightPels; i++)
        {
            memcpy(destPtr + i * destPitch, pBitmapData + i * abs(destPitch), abs(destPitch)-1);
        }
        hDstDC = destImage2.GetDC();
        SetStretchBltMode(hDstDC,HALFTONE);
        destImage.StretchBlt(hDstDC, 0, 0, m_format.rcPicture.right, m_format.rcPicture.bottom, SRCCOPY);
        destImage2.ReleaseDC();
        hr = destImage2.Save(strFileName);
        
    }
    else
    {
        hr = MF_E_END_OF_STREAM;
    }

done:

    if (pBitmapData)
    {
        pBuffer->Unlock();
    }
    SafeRelease(&pBuffer);

    return hr;
}

//-------------------------------------------------------------------
// CreateBitmap
//
// Creates video thumbnail.
//
// pRT:      Direct2D render target. Used to create the bitmap.
// hnsPos:   The seek position.
// pSprite:  A Sprite object to hold the bitmap.
//-------------------------------------------------------------------

HRESULT CreateBitmap(LONGLONG& hnsPos, UINT16 frame_num, bool seek_interval=true)
{
    HRESULT     hr = S_OK;
    DWORD       dwFlags = 0;

    BYTE* pBitmapData = NULL;    // Bitmap data
    DWORD       cbBitmapData = 0;       // Size of data, in bytes
    LONGLONG    hnsTimeStamp = 0;
    DWORD       cSkipped = 0;           // Number of skipped frames

    IMFMediaBuffer* pBuffer = 0;
    IMFSample* pSample = NULL;

    if (hnsPos > 0)
    {
        PROPVARIANT var;
        PropVariantInit(&var);

        var.vt = VT_I8;
        var.hVal.QuadPart = hnsPos;
        hr = m_pReader->SetCurrentPosition(GUID_NULL, var);
        PropVariantClear(&var);
        if (FAILED(hr)) { goto done; }

    }


    // Pulls video frames from the source reader.

    // NOTE: Seeking might be inaccurate, depending on the container
    //       format and how the file was indexed. Therefore, the first
    //       frame that we get might be earlier than the desired time.
    //       If so, we skip up to MAX_FRAMES_TO_SKIP frames.

    while (1)
    {
        IMFSample* pSampleTmp = NULL;

        hr = m_pReader->ReadSample(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            NULL,
            &dwFlags,
            NULL,
            &pSampleTmp
        );

        if (FAILED(hr)) { goto done; }

        if (dwFlags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            break;
        }

        if (dwFlags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
        {
            // Type change. Get the new format.
            hr = GetVideoFormat();

            if (FAILED(hr)) { goto done; }
        }

        if (pSampleTmp == NULL)
        {
            continue;
        }

        // We got a sample. Hold onto it.

        SafeRelease(&pSample);

        pSample = pSampleTmp;
        pSample->AddRef();

        if (SUCCEEDED(pSample->GetSampleTime(&hnsTimeStamp)))
        {
            // Keep going until we get a frame that is within tolerance of the
            // desired seek position, or until we skip MAX_FRAMES_TO_SKIP frames.

            // During this process, we might reach the end of the file, so we
            // always cache the last sample that we got (pSample).
			if (seek_interval){
				if ((cSkipped < MAX_FRAMES_TO_SKIP) &&
					(hnsTimeStamp + SEEK_TOLERANCE < hnsPos))
				{
					SafeRelease(&pSampleTmp);
					++cSkipped;
					continue;
				}
			}else{
				if (cSkipped < frame_num)
				{	
					SafeRelease(&pSampleTmp);
					save_thumb(pSample, hnsTimeStamp, cSkipped);
					++cSkipped;
					continue;
				}else{
					goto done;
				}
			}
        }

        SafeRelease(&pSampleTmp);

        hnsPos = hnsTimeStamp;
        break;
    }
    // seek_interval is true do this.
    save_thumb(pSample, hnsTimeStamp, frame_num);

done:

    if (pBitmapData)
    {
        pBuffer->Unlock();
    }
    SafeRelease(&pBuffer);
    SafeRelease(&pSample);

    return hr;
}



//-------------------------------------------------------------------
// CreateBitmaps
//
// Creates an array of thumbnails from the video file.
//
// count:    Number of thumbnails to create.
// seek_interval:  need seek with average interval
//
// Note: The caller allocates the sprite objects.
//-------------------------------------------------------------------

HRESULT CreateBitmaps(
    UINT16 count,
    bool seek_interval
)
{
    HRESULT hr = S_OK;
    BOOL bCanSeek = 0;

    LONGLONG hnsDuration = 0;
    LONGLONG hnsRangeStart = 0;
    LONGLONG hnsRangeEnd = 0;
    LONGLONG hnsIncrement = 0;

    hr = CanSeek(&bCanSeek);

    if (FAILED(hr)) { return hr; }

    if (bCanSeek)
    {
        hr = GetDuration(&hnsDuration);

        if (FAILED(hr)) { return hr; }

        hnsRangeStart = 0;
        // 'duration' is in 100ns units, so need /10000000 
        hnsRangeEnd = hnsDuration;

        // We have the file duration , so we'll take bitmaps from
        // several positions in the file. Occasionally, the first frame 
        // in a video is black, so we don't start at time 0.

        hnsIncrement = (hnsRangeEnd - hnsRangeStart) / (count + 1);

    }
    if (seek_interval) {
        // Generate the bitmaps and invalidate the button controls so
        // they will be redrawn.
        for (UINT16 i = 0; i < count; i++)
        {
            LONGLONG hPos = hnsIncrement * (i + 1);
            hr = CreateBitmap(hPos, i);
        }
    }
    else { 
        LONGLONG hPos = 150000000;
        hr = CreateBitmap(hPos, count, seek_interval);
    }

    return hr;
}


BOOL InitializeApp()
{
    HRESULT hr = S_OK;

    // Initialize COM
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    if (SUCCEEDED(hr))
    {
        // Initialize Media Foundation.
        hr = MFStartup(MF_VERSION);
    }
    
	// Initialize GDI+
    /*
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    */

    return (SUCCEEDED(hr));
}

int _tmain(int argc, _TCHAR* argv[])
{
	HRESULT hr = S_OK;
    UINT16 number = 5;
    bool seek_interval = true;
    const WCHAR*  video_path = argv[1];
    if (argc > 2) {
        number = _wtoi(argv[2]);
        if (argc > 3) {
            seek_interval = _wtoi(argv[3]) != 0;
        }
    }
    
    ZeroMemory(&m_format, sizeof(m_format));
    InitializeApp();
    hr = OpenFile(video_path);

    if (FAILED(hr))
    {
        cout << "Open file error";
        return hr;
    }
   hr = CreateBitmaps(number, seek_interval);
   if (FAILED(hr))
   {
       cout << " Create Bitmaps error";
       return hr;
   }
}
