#include "KRMovieLayer.h"
#include "VideoCodec.h"
#include "LayerBitmapIntf.h"
#include "Application.h"
#include "VideoOvlImpl.h"
#include "SDL_log.h"
extern "C" {
#include "libswscale/swscale.h"
}

NS_KRMOVIE_BEGIN

VideoPresentLayer::~VideoPresentLayer()
{
	TVPRemoveContinuousEventHook(this);
	// The decoder calls this derived renderer. Join it before its vtable and
	// RGBA presentation state are torn down.
	m_pPlayer->CloseInputStream();
}

tTVPBaseTexture* VideoPresentLayer::GetFrontBuffer()
{
	if (!m_BmpBits[0] || !m_BmpBits[1] || !TakeDuePicture()) return nullptr;
	BitmapPicture &pic = m_presentPicture;
	int n = m_nCurBmpBuff;
	m_nCurBmpBuff = !m_nCurBmpBuff;
	m_BmpBits[n]->Update(pic.data[0], pic.width * 4, 0, 0, pic.width, pic.height);
	return m_BmpBits[n];
}

void VideoPresentLayer::SetVideoBuffer(tTVPBaseTexture *buff1, tTVPBaseTexture *buff2, long size)
{
	m_BmpBits[0] = buff1;
	m_BmpBits[1] = buff2;
	m_nCurBmpBuff = 0;
//	TVPAddContinuousEventHook(this);
}

void VideoPresentLayer::OnContinuousCallback(tjs_uint64 tick)
{
	FrameMove();
	double clock = m_pPlayer->GetClock() / DVD_TIME_BASE;
	{
		std::lock_guard<std::mutex> lk(m_mtxPicture);
		if (!m_usedPicture) return;
		BitmapPicture &picbuf = m_picture[m_curPicture];
		// check pts
		if (picbuf.pts > clock) { // present in future
			return;
		}
	}
	OnPlayEvent(KRMovieEvent::Update, nullptr);
}

int VideoPresentLayer::AddVideoPicture(DVDVideoPicture &pic, int index)
{
	// from other thread
	if (pic.format != RENDER_FMT_YUV420P) return -2;
	if (pic.pts == DVD_NOPTS_VALUE) return 0;

	std::lock_guard<std::mutex> lk(m_mtxPicture);
	if (m_usedPicture >= MAX_BUFFER_COUNT) return -1;

	int width = pic.iWidth, height = pic.iHeight;
	BitmapPicture &picbuf = m_picture[(m_curPicture + m_usedPicture) & (MAX_BUFFER_COUNT - 1)];
	if (!picbuf.data[0] || picbuf.width != width || picbuf.height != height) {
		picbuf.Clear();
		picbuf.width = width;
		picbuf.height = height;
		picbuf.data[0] = (uint8_t*)TJSAlignedAlloc(width * height * 4, 4);
	}
	uint8_t *data = picbuf.data[0];
	int datasize = width * 4;

	img_convert_ctx = sws_getCachedContext(
		img_convert_ctx, width, height, AV_PIX_FMT_YUV420P, width, height,
		AV_PIX_FMT_RGBA, /*sws_flags*/SWS_FAST_BILINEAR, NULL, NULL, NULL);
	if (!img_convert_ctx) return -2;
	int processed = sws_scale(img_convert_ctx, pic.data, pic.iLineSize, 0, pic.iHeight, &data, &datasize);
	if (processed != height) return -2;
	picbuf.pts = pic.pts / DVD_TIME_BASE;
	++m_usedPicture;
	if (!m_loggedFirstPicture) {
		SDL_Log("Movie: first decoded layer picture %dx%d pts=%.3f", width, height, picbuf.pts);
		m_loggedFirstPicture = true;
	}

	return MAX_BUFFER_COUNT - m_usedPicture;
}

void MoviePlayerLayer::BuildGraph(tTJSNI_VideoOverlay* callbackwin, IStream *stream, const tjs_char * streamname, const tjs_char *type, uint64_t size)
{
	m_pCallbackWin = callbackwin;
	m_pPlayer->SetCallback(std::bind(&MoviePlayerLayer::OnPlayEvent, this, std::placeholders::_1, std::placeholders::_2));
	m_pPlayer->OpenFromStream(stream, streamname, type, size);
}

void MoviePlayerLayer::OnPlayEvent(KRMovieEvent msg, void *p)
{
	if (msg == KRMovieEvent::Update) {
		NativeEvent ev(WM_GRAPHNOTIFY);
		ev.WParam = EC_UPDATE;
		int frame; GetFrame(&frame);
		ev.LParam = frame;
		m_pCallbackWin->WndProc(ev); // in the same thread
	} else if (msg == KRMovieEvent::Ended) {
		NativeEvent ev(WM_GRAPHNOTIFY);
		ev.WParam = EC_COMPLETE;
		ev.LParam = 0;
		m_pCallbackWin->PostEvent(ev);
	}
}

void MoviePlayerLayer::Play()
{
	inherit::Play();
	TVPAddContinuousEventHook(this);
}

NS_KRMOVIE_END
