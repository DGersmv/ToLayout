#include "RandomizerPalette.hpp"

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "DGBrowser.hpp"
#include "BrowserRepl.hpp"

static GS::UniString LoadRandomizerHtml()
{
	GS::UniString html;
	GSHandle data = RSLoadResource('DATA', ACAPI_GetOwnResModule(), RandomizerHtmlResId);
	if (data != nullptr) {
		const GSSize size = BMhGetSize(data);
		html.Append(*data, size);
		BMhKill(&data);
	} else {
		html = "<html><body style='font-family:Arial;color:#900;padding:12px;'>"
			"Не удалось загрузить HTML ресурса рандомизатора."
			"</body></html>";
	}
	return html;
}

static GSErrCode RandomizerPaletteCallback(Int32 /*refCon*/, API_PaletteMessageID messageID, GS::IntPtr param)
{
	switch (messageID) {
	case APIPalMsg_OpenPalette:
		if (!RandomizerPalette::HasInstance()) RandomizerPalette::CreateInstance();
		RandomizerPalette::GetInstance().Show();
		break;

	case APIPalMsg_ClosePalette:
		if (RandomizerPalette::HasInstance()) RandomizerPalette::GetInstance().Hide();
		break;

	case APIPalMsg_HidePalette_Begin:
		if (RandomizerPalette::HasInstance() && RandomizerPalette::GetInstance().IsVisible())
			RandomizerPalette::GetInstance().Hide();
		break;

	case APIPalMsg_HidePalette_End:
		if (RandomizerPalette::HasInstance() && !RandomizerPalette::GetInstance().IsVisible())
			RandomizerPalette::GetInstance().Show();
		break;

	case APIPalMsg_DisableItems_Begin:
		if (RandomizerPalette::HasInstance() && RandomizerPalette::GetInstance().IsVisible())
			RandomizerPalette::GetInstance().DisableItems();
		break;

	case APIPalMsg_DisableItems_End:
		if (RandomizerPalette::HasInstance() && RandomizerPalette::GetInstance().IsVisible())
			RandomizerPalette::GetInstance().EnableItems();
		break;

	case APIPalMsg_IsPaletteVisible:
		if (param != 0)
			*reinterpret_cast<bool*>(param) = RandomizerPalette::HasInstance() && RandomizerPalette::GetInstance().IsVisible();
		break;

	default:
		break;
	}
	return NoError;
}

GS::Ref<RandomizerPalette> RandomizerPalette::s_instance(nullptr);
const GS::Guid RandomizerPalette::s_guid("{a8b9c7d6-e5f4-4321-9876-543210fedcba}");

RandomizerPalette::RandomizerPalette()
	: DG::Palette(ACAPI_GetOwnResModule(), RandomizerPaletteResId, ACAPI_GetOwnResModule(), s_guid)
{
	m_browserCtrl = new DG::Browser(GetReference(), RandomizerBrowserCtrlId);
	Attach(*this);
	BeginEventProcessing();
	Init();
}

RandomizerPalette::~RandomizerPalette()
{
	EndEventProcessing();
	delete m_browserCtrl;
	m_browserCtrl = nullptr;
}

void RandomizerPalette::Init()
{
	LoadHtml();
	if (m_browserCtrl != nullptr)
		BrowserRepl::RegisterACAPIJavaScriptObject(*m_browserCtrl);
}

void RandomizerPalette::LoadHtml()
{
	if (m_browserCtrl == nullptr)
		return;

	const GS::UniString html = LoadRandomizerHtml();
	m_browserCtrl->LoadHTML(html);
}

bool RandomizerPalette::HasInstance()
{
	return s_instance != nullptr;
}

void RandomizerPalette::CreateInstance()
{
	DBASSERT(!HasInstance());
	s_instance = new RandomizerPalette();
	ACAPI_KeepInMemory(true);
}

RandomizerPalette& RandomizerPalette::GetInstance()
{
	DBASSERT(HasInstance());
	return *s_instance;
}

void RandomizerPalette::DestroyInstance()
{
	s_instance = nullptr;
}

void RandomizerPalette::ShowPalette()
{
	if (!HasInstance())
		CreateInstance();

	GetInstance().Show();
}

void RandomizerPalette::HidePalette()
{
	if (!HasInstance())
		return;

	GetInstance().Hide();
}

GSErrCode RandomizerPalette::RegisterPaletteControlCallBack()
{
	return ACAPI_RegisterModelessWindow(
		GS::CalculateHashValue(s_guid),
		RandomizerPaletteCallback,
		API_PalEnabled_FloorPlan |
		API_PalEnabled_Section |
		API_PalEnabled_Elevation |
		API_PalEnabled_InteriorElevation |
		API_PalEnabled_3D |
		API_PalEnabled_Detail |
		API_PalEnabled_Worksheet |
		API_PalEnabled_Layout |
		API_PalEnabled_DocumentFrom3D,
		GSGuid2APIGuid(s_guid)
	);
}

void RandomizerPalette::PanelResized(const DG::PanelResizeEvent& ev)
{
	BeginMoveResizeItems();
	if (m_browserCtrl != nullptr)
		m_browserCtrl->Resize(ev.GetHorizontalChange(), ev.GetVerticalChange());
	EndMoveResizeItems();
}

void RandomizerPalette::PanelCloseRequested(const DG::PanelCloseRequestEvent&, bool* accepted)
{
	HidePalette();
	if (accepted != nullptr)
		*accepted = true;
}

