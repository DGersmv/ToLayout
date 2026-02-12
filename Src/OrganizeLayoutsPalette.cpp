#include "OrganizeLayoutsPalette.hpp"

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "DGBrowser.hpp"
#include "BrowserRepl.hpp"

static GS::UniString LoadOrganizeLayoutsHtml()
{
	GS::UniString html;
	GSHandle data = RSLoadResource('DATA', ACAPI_GetOwnResModule(), OrganizeLayoutsHtmlResId);
	if (data != nullptr) {
		const GSSize size = BMhGetSize(data);
		html.Append(*data, size);
		BMhKill(&data);
	} else {
		html = "<html><body style='font-family:Arial;color:#900;padding:12px;'>"
			"Не удалось загрузить HTML палитры «Организация чертежей в макетах»."
			"</body></html>";
	}
	return html;
}

static GSErrCode OrganizeLayoutsPaletteCallback(Int32 /*refCon*/, API_PaletteMessageID messageID, GS::IntPtr param)
{
	switch (messageID) {
	case APIPalMsg_OpenPalette:
		if (!OrganizeLayoutsPalette::HasInstance()) OrganizeLayoutsPalette::CreateInstance();
		OrganizeLayoutsPalette::GetInstance().Show();
		break;

	case APIPalMsg_ClosePalette:
		if (OrganizeLayoutsPalette::HasInstance()) OrganizeLayoutsPalette::GetInstance().Hide();
		break;

	case APIPalMsg_HidePalette_Begin:
		if (OrganizeLayoutsPalette::HasInstance() && OrganizeLayoutsPalette::GetInstance().IsVisible())
			OrganizeLayoutsPalette::GetInstance().Hide();
		break;

	case APIPalMsg_HidePalette_End:
		if (OrganizeLayoutsPalette::HasInstance() && !OrganizeLayoutsPalette::GetInstance().IsVisible())
			OrganizeLayoutsPalette::GetInstance().Show();
		break;

	case APIPalMsg_DisableItems_Begin:
		if (OrganizeLayoutsPalette::HasInstance() && OrganizeLayoutsPalette::GetInstance().IsVisible())
			OrganizeLayoutsPalette::GetInstance().DisableItems();
		break;

	case APIPalMsg_DisableItems_End:
		if (OrganizeLayoutsPalette::HasInstance() && OrganizeLayoutsPalette::GetInstance().IsVisible())
			OrganizeLayoutsPalette::GetInstance().EnableItems();
		break;

	case APIPalMsg_IsPaletteVisible:
		if (param != 0)
			*reinterpret_cast<bool*>(param) = OrganizeLayoutsPalette::HasInstance() && OrganizeLayoutsPalette::GetInstance().IsVisible();
		break;

	default:
		break;
	}
	return NoError;
}

GS::Ref<OrganizeLayoutsPalette> OrganizeLayoutsPalette::s_instance(nullptr);
const GS::Guid OrganizeLayoutsPalette::s_guid("{7e819203-4567-3456-f012-567890123456}");

OrganizeLayoutsPalette::OrganizeLayoutsPalette()
	: DG::Palette(ACAPI_GetOwnResModule(), OrganizeLayoutsPaletteResId, ACAPI_GetOwnResModule(), s_guid)
{
	m_browserCtrl = new DG::Browser(GetReference(), OrganizeLayoutsBrowserCtrlId);
	Attach(*this);
	BeginEventProcessing();

	Init();
}

OrganizeLayoutsPalette::~OrganizeLayoutsPalette()
{
	EndEventProcessing();
	delete m_browserCtrl;
	m_browserCtrl = nullptr;
}

void OrganizeLayoutsPalette::Init()
{
	LoadHtml();
	if (m_browserCtrl != nullptr)
		BrowserRepl::RegisterACAPIJavaScriptObject(*m_browserCtrl);
}

void OrganizeLayoutsPalette::LoadHtml()
{
	if (m_browserCtrl == nullptr)
		return;

	const GS::UniString html = LoadOrganizeLayoutsHtml();
	m_browserCtrl->LoadHTML(html);
}

bool OrganizeLayoutsPalette::HasInstance()
{
	return s_instance != nullptr;
}

void OrganizeLayoutsPalette::CreateInstance()
{
	DBASSERT(!HasInstance());
	s_instance = new OrganizeLayoutsPalette();
	ACAPI_KeepInMemory(true);
}

OrganizeLayoutsPalette& OrganizeLayoutsPalette::GetInstance()
{
	DBASSERT(HasInstance());
	return *s_instance;
}

void OrganizeLayoutsPalette::DestroyInstance()
{
	s_instance = nullptr;
}

void OrganizeLayoutsPalette::ShowPalette()
{
	if (!HasInstance())
		CreateInstance();

	GetInstance().Show();
}

void OrganizeLayoutsPalette::HidePalette()
{
	if (!HasInstance())
		return;

	GetInstance().Hide();
}

GSErrCode OrganizeLayoutsPalette::RegisterPaletteControlCallBack()
{
	return ACAPI_RegisterModelessWindow(
		GS::CalculateHashValue(s_guid),
		OrganizeLayoutsPaletteCallback,
		API_PalEnabled_FloorPlan |
		API_PalEnabled_Section |
		API_PalEnabled_Elevation |
		API_PalEnabled_InteriorElevation |
		API_PalEnabled_Detail |
		API_PalEnabled_Worksheet |
		API_PalEnabled_DocumentFrom3D |
		API_PalEnabled_Layout,
		GSGuid2APIGuid(s_guid)
	);
}

void OrganizeLayoutsPalette::PanelResized(const DG::PanelResizeEvent& ev)
{
	BeginMoveResizeItems();
	if (m_browserCtrl != nullptr)
		m_browserCtrl->Resize(ev.GetHorizontalChange(), ev.GetVerticalChange());
	EndMoveResizeItems();
}

void OrganizeLayoutsPalette::PanelCloseRequested(const DG::PanelCloseRequestEvent&, bool* accepted)
{
	HidePalette();
	if (accepted != nullptr)
		*accepted = true;
}
