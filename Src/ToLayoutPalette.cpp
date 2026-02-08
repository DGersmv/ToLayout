#include "ToLayoutPalette.hpp"

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "DGBrowser.hpp"
#include "BrowserRepl.hpp"

static GS::UniString LoadToLayoutHtml()
{
	GS::UniString html;
	GSHandle data = RSLoadResource('DATA', ACAPI_GetOwnResModule(), ToLayoutHtmlResId);
	if (data != nullptr) {
		const GSSize size = BMhGetSize(data);
		html.Append(*data, size);
		BMhKill(&data);
	} else {
		html = "<html><body style='font-family:Arial;color:#900;padding:12px;'>"
			"Не удалось загрузить HTML палитры «Расположить в макет»."
			"</body></html>";
	}
	return html;
}

static GSErrCode ToLayoutPaletteCallback(Int32 /*refCon*/, API_PaletteMessageID messageID, GS::IntPtr param)
{
	switch (messageID) {
	case APIPalMsg_OpenPalette:
		if (!ToLayoutPalette::HasInstance()) ToLayoutPalette::CreateInstance();
		ToLayoutPalette::GetInstance().Show();
		break;

	case APIPalMsg_ClosePalette:
		if (ToLayoutPalette::HasInstance()) ToLayoutPalette::GetInstance().Hide();
		break;

	case APIPalMsg_HidePalette_Begin:
		if (ToLayoutPalette::HasInstance() && ToLayoutPalette::GetInstance().IsVisible())
			ToLayoutPalette::GetInstance().Hide();
		break;

	case APIPalMsg_HidePalette_End:
		if (ToLayoutPalette::HasInstance() && !ToLayoutPalette::GetInstance().IsVisible())
			ToLayoutPalette::GetInstance().Show();
		break;

	case APIPalMsg_DisableItems_Begin:
		if (ToLayoutPalette::HasInstance() && ToLayoutPalette::GetInstance().IsVisible())
			ToLayoutPalette::GetInstance().DisableItems();
		break;

	case APIPalMsg_DisableItems_End:
		if (ToLayoutPalette::HasInstance() && ToLayoutPalette::GetInstance().IsVisible())
			ToLayoutPalette::GetInstance().EnableItems();
		break;

	case APIPalMsg_IsPaletteVisible:
		if (param != 0)
			*reinterpret_cast<bool*>(param) = ToLayoutPalette::HasInstance() && ToLayoutPalette::GetInstance().IsVisible();
		break;

	default:
		break;
	}
	return NoError;
}

GS::Ref<ToLayoutPalette> ToLayoutPalette::s_instance(nullptr);
const GS::Guid ToLayoutPalette::s_guid("{6f708192-3456-2345-f012-456789012345}");

ToLayoutPalette::ToLayoutPalette()
	: DG::Palette(ACAPI_GetOwnResModule(), ToLayoutPaletteResId, ACAPI_GetOwnResModule(), s_guid)
{
	m_browserCtrl = new DG::Browser(GetReference(), ToLayoutBrowserCtrlId);
	Attach(*this);
	BeginEventProcessing();

	ACAPI_Notification_CatchSelectionChange(SelectionChangeHandler);

	Init();
}

ToLayoutPalette::~ToLayoutPalette()
{
	EndEventProcessing();
	delete m_browserCtrl;
	m_browserCtrl = nullptr;
}

void ToLayoutPalette::Init()
{
	LoadHtml();
	if (m_browserCtrl != nullptr)
		BrowserRepl::RegisterACAPIJavaScriptObject(*m_browserCtrl);
}

void ToLayoutPalette::LoadHtml()
{
	if (m_browserCtrl == nullptr)
		return;

	const GS::UniString html = LoadToLayoutHtml();
	m_browserCtrl->LoadHTML(html);
}

bool ToLayoutPalette::HasInstance()
{
	return s_instance != nullptr;
}

void ToLayoutPalette::CreateInstance()
{
	DBASSERT(!HasInstance());
	s_instance = new ToLayoutPalette();
	ACAPI_KeepInMemory(true);
}

ToLayoutPalette& ToLayoutPalette::GetInstance()
{
	DBASSERT(HasInstance());
	return *s_instance;
}

void ToLayoutPalette::DestroyInstance()
{
	s_instance = nullptr;
}

void ToLayoutPalette::ShowPalette()
{
	if (!HasInstance())
		CreateInstance();

	GetInstance().Show();
}

void ToLayoutPalette::HidePalette()
{
	if (!HasInstance())
		return;

	GetInstance().Hide();
}

void ToLayoutPalette::UpdateSelectionListOnHTML()
{
	if (!HasInstance() || !GetInstance().IsVisible())
		return;

	if (GetInstance().m_browserCtrl != nullptr)
		GetInstance().m_browserCtrl->ExecuteJS("UpdateSelectionList()");
}

GSErrCode ToLayoutPalette::SelectionChangeHandler(const API_Neig* neig)
{
	(void)neig;
	if (HasInstance())
		UpdateSelectionListOnHTML();
	return NoError;
}

GSErrCode ToLayoutPalette::RegisterPaletteControlCallBack()
{
	return ACAPI_RegisterModelessWindow(
		GS::CalculateHashValue(s_guid),
		ToLayoutPaletteCallback,
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

void ToLayoutPalette::PanelResized(const DG::PanelResizeEvent& ev)
{
	BeginMoveResizeItems();
	if (m_browserCtrl != nullptr)
		m_browserCtrl->Resize(ev.GetHorizontalChange(), ev.GetVerticalChange());
	EndMoveResizeItems();
}

void ToLayoutPalette::PanelCloseRequested(const DG::PanelCloseRequestEvent&, bool* accepted)
{
	HidePalette();
	if (accepted != nullptr)
		*accepted = true;
}
