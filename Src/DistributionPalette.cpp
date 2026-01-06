#include "DistributionPalette.hpp"

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "DGBrowser.hpp"
#include "BrowserRepl.hpp"

// -------------------- local helpers --------------------
static GS::UniString LoadDistributionHtml()
{
	GS::UniString html;
	GSHandle data = RSLoadResource('DATA', ACAPI_GetOwnResModule(), DistributionHtmlResId);
	if (data != nullptr) {
		const GSSize size = BMhGetSize(data);
		html.Append(*data, size);
		BMhKill(&data);
	} else {
		html = "<html><body style='font-family:Arial;color:#900;padding:12px;'>"
			"Не удалось загрузить HTML ресурса распределения.""</body></html>";
	}
	return html;
}

static GSErrCode DistributionPaletteCallback(Int32 /*refCon*/, API_PaletteMessageID messageID, GS::IntPtr param)
{
	switch (messageID) {
	case APIPalMsg_OpenPalette:
		if (!DistributionPalette::HasInstance()) DistributionPalette::CreateInstance();
		DistributionPalette::GetInstance().Show();
		break;

	case APIPalMsg_ClosePalette:
		if (DistributionPalette::HasInstance()) DistributionPalette::GetInstance().Hide();
		break;

	case APIPalMsg_HidePalette_Begin:
		if (DistributionPalette::HasInstance() && DistributionPalette::GetInstance().IsVisible())
			DistributionPalette::GetInstance().Hide();
		break;

	case APIPalMsg_HidePalette_End:
		if (DistributionPalette::HasInstance() && !DistributionPalette::GetInstance().IsVisible())
			DistributionPalette::GetInstance().Show();
		break;

	case APIPalMsg_DisableItems_Begin:
		if (DistributionPalette::HasInstance() && DistributionPalette::GetInstance().IsVisible())
			DistributionPalette::GetInstance().DisableItems();
		break;

	case APIPalMsg_DisableItems_End:
		if (DistributionPalette::HasInstance() && DistributionPalette::GetInstance().IsVisible())
			DistributionPalette::GetInstance().EnableItems();
		break;

	case APIPalMsg_IsPaletteVisible:
		if (param != 0)
			*reinterpret_cast<bool*>(param) = DistributionPalette::HasInstance() && DistributionPalette::GetInstance().IsVisible();
		break;

	default:
		break;
	}
	return NoError;
}

// -------------------- static members --------------------
GS::Ref<DistributionPalette> DistributionPalette::s_instance(nullptr);
const GS::Guid DistributionPalette::s_guid("{6af9d6d5-3f54-4b91-a497-7a7d96b90e1e}");

// -------------------- lifecycle --------------------
DistributionPalette::DistributionPalette()
	: DG::Palette(ACAPI_GetOwnResModule(), DistributionPaletteResId, ACAPI_GetOwnResModule(), s_guid)
{
	m_browserCtrl = new DG::Browser(GetReference(), DistributionBrowserCtrlId);
	Attach(*this);
	BeginEventProcessing();
	Init();
}

DistributionPalette::~DistributionPalette()
{
	EndEventProcessing();
	delete m_browserCtrl;
	m_browserCtrl = nullptr;
}

void DistributionPalette::Init()
{
	LoadHtml();
	if (m_browserCtrl != nullptr)
		BrowserRepl::RegisterACAPIJavaScriptObject(*m_browserCtrl);
}

void DistributionPalette::LoadHtml()
{
	if (m_browserCtrl == nullptr)
		return;

	const GS::UniString html = LoadDistributionHtml();
	m_browserCtrl->LoadHTML(html);
}

// -------------------- singleton helpers --------------------
bool DistributionPalette::HasInstance()
{
	return s_instance != nullptr;
}

void DistributionPalette::CreateInstance()
{
	DBASSERT(!HasInstance());
	s_instance = new DistributionPalette();
	ACAPI_KeepInMemory(true);
}

DistributionPalette& DistributionPalette::GetInstance()
{
	DBASSERT(HasInstance());
	return *s_instance;
}

void DistributionPalette::DestroyInstance()
{
	s_instance = nullptr;
}

// -------------------- public API --------------------
void DistributionPalette::ShowPalette()
{
	if (!HasInstance())
		CreateInstance();

	GetInstance().Show();
}

void DistributionPalette::HidePalette()
{
	if (!HasInstance())
		return;

	GetInstance().Hide();
}

GSErrCode DistributionPalette::RegisterPaletteControlCallBack()
{
	return ACAPI_RegisterModelessWindow(
		GS::CalculateHashValue(s_guid),
		DistributionPaletteCallback,
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

// -------------------- DG::PanelObserver --------------------
void DistributionPalette::PanelResized(const DG::PanelResizeEvent& ev)
{
	BeginMoveResizeItems();
	if (m_browserCtrl != nullptr)
		m_browserCtrl->Resize(ev.GetHorizontalChange(), ev.GetVerticalChange());
	EndMoveResizeItems();
}

void DistributionPalette::PanelCloseRequested(const DG::PanelCloseRequestEvent&, bool* accepted)
{
	HidePalette();
	if (accepted != nullptr)
		*accepted = true;
}
