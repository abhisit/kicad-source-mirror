/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2012-2015 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2008-2016 Wayne Stambaugh <stambaughw@verizon.net>
 * Copyright (C) 2004-2017 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * @file footprint_viewer_frame.cpp
 */

#include <fctsys.h>
#include <pgm_base.h>
#include <kiway.h>
#include <class_drawpanel.h>
#include <pcb_draw_panel_gal.h>
#include <3d_viewer/eda_3d_viewer.h>
#include <dialog_helpers.h>
#include <msgpanel.h>
#include <fp_lib_table.h>
#include <lib_id.h>
#include <confirm.h>
#include <bitmaps.h>
#include <gal/graphics_abstraction_layer.h>

#include <class_board.h>
#include <class_module.h>

#include <pcbnew.h>
#include <pcbnew_id.h>
#include <footprint_viewer_frame.h>
#include <footprint_info.h>

#include <hotkeys.h>
#include <wildcards_and_files_ext.h>
#include <config_params.h>

#include <tool/tool_manager.h>
#include <tool/tool_dispatcher.h>
#include <tool/common_tools.h>
#include "tools/selection_tool.h"
#include "tools/pcbnew_control.h"
#include "tools/pcb_actions.h"

#include <functional>
#include <memory>
using namespace std::placeholders;


#define NEXT_PART       1
#define NEW_PART        0
#define PREVIOUS_PART   -1


BEGIN_EVENT_TABLE( FOOTPRINT_VIEWER_FRAME, EDA_DRAW_FRAME )
    // Window events
    EVT_CLOSE( FOOTPRINT_VIEWER_FRAME::OnCloseWindow )
    EVT_SIZE( FOOTPRINT_VIEWER_FRAME::OnSize )
    EVT_ACTIVATE( FOOTPRINT_VIEWER_FRAME::OnActivate )

    // Menu (and/or hotkey) events
    EVT_MENU( wxID_EXIT, FOOTPRINT_VIEWER_FRAME::CloseFootprintViewer )
    EVT_MENU( ID_SET_RELATIVE_OFFSET, FOOTPRINT_VIEWER_FRAME::OnSetRelativeOffset )

    // Menu Help
    EVT_MENU( wxID_HELP, EDA_DRAW_FRAME::GetKicadHelp )
    EVT_MENU( wxID_INDEX, EDA_DRAW_FRAME::GetKicadHelp )
    EVT_MENU( ID_HELP_GET_INVOLVED, EDA_DRAW_FRAME::GetKicadContribute )
    EVT_MENU( wxID_ABOUT, EDA_BASE_FRAME::GetKicadAbout )

    // Toolbar events
    EVT_TOOL( ID_MODVIEW_SELECT_LIB,
              FOOTPRINT_VIEWER_FRAME::SelectCurrentLibrary )
    EVT_TOOL( ID_MODVIEW_SELECT_PART,
              FOOTPRINT_VIEWER_FRAME::SelectCurrentFootprint )
    EVT_TOOL( ID_MODVIEW_NEXT,
              FOOTPRINT_VIEWER_FRAME::OnIterateFootprintList )
    EVT_TOOL( ID_MODVIEW_PREVIOUS,
              FOOTPRINT_VIEWER_FRAME::OnIterateFootprintList )
    EVT_TOOL( ID_MODVIEW_FOOTPRINT_EXPORT_TO_BOARD,
              FOOTPRINT_VIEWER_FRAME::ExportSelectedFootprint )
    EVT_TOOL( ID_MODVIEW_SHOW_3D_VIEW, FOOTPRINT_VIEWER_FRAME::Show3D_Frame )

    // listbox events
    EVT_LISTBOX( ID_MODVIEW_LIB_LIST, FOOTPRINT_VIEWER_FRAME::ClickOnLibList )
    EVT_LISTBOX( ID_MODVIEW_FOOTPRINT_LIST, FOOTPRINT_VIEWER_FRAME::ClickOnFootprintList )
    EVT_LISTBOX_DCLICK( ID_MODVIEW_FOOTPRINT_LIST, FOOTPRINT_VIEWER_FRAME::DClickOnFootprintList )

END_EVENT_TABLE()


/* Note:
 * FOOTPRINT_VIEWER_FRAME can be created in "modal mode", or as a usual frame.
 * In modal mode:
 *  a tool to export the selected footprint is shown in the toolbar
 *  the style is wxFRAME_FLOAT_ON_PARENT
 * Note:
 * On windows, when the frame with type wxFRAME_FLOAT_ON_PARENT is displayed
 * its parent frame is sometimes brought to the foreground when closing the
 * LIB_VIEW_FRAME frame.
 * If it still happens, it could be better to use wxSTAY_ON_TOP
 * instead of wxFRAME_FLOAT_ON_PARENT
 */
#ifdef __WINDOWS__
#define MODAL_MODE_EXTRASTYLE wxFRAME_FLOAT_ON_PARENT   // could be wxSTAY_ON_TOP if issues
#else
#define MODAL_MODE_EXTRASTYLE wxFRAME_FLOAT_ON_PARENT
#endif


FOOTPRINT_VIEWER_FRAME::FOOTPRINT_VIEWER_FRAME( KIWAY* aKiway, wxWindow* aParent, FRAME_T aFrameType ) :
    PCB_BASE_FRAME( aKiway, aParent, aFrameType, _( "Footprint Library Browser" ),
            wxDefaultPosition, wxDefaultSize,
            aFrameType == FRAME_PCB_MODULE_VIEWER_MODAL ?
                    aParent ?
                        KICAD_DEFAULT_DRAWFRAME_STYLE | MODAL_MODE_EXTRASTYLE
                        : KICAD_DEFAULT_DRAWFRAME_STYLE | wxSTAY_ON_TOP
                : KICAD_DEFAULT_DRAWFRAME_STYLE,
            aFrameType == FRAME_PCB_MODULE_VIEWER_MODAL ?
                                FOOTPRINT_VIEWER_FRAME_NAME_MODAL
                                : FOOTPRINT_VIEWER_FRAME_NAME )
{
    wxASSERT( aFrameType == FRAME_PCB_MODULE_VIEWER_MODAL ||
              aFrameType == FRAME_PCB_MODULE_VIEWER );

    if( aFrameType == FRAME_PCB_MODULE_VIEWER_MODAL )
        SetModal( true );

    // Force the frame name used in config. the footprint viewer frame has a name
    // depending on aFrameType (needed to identify the frame by wxWidgets),
    // but only one configuration is preferable.
    m_configFrameName = FOOTPRINT_VIEWER_FRAME_NAME;

    m_showAxis   = true;         // true to draw axis.

    // Give an icon
    wxIcon  icon;
    icon.CopyFromBitmap( KiBitmap( modview_icon_xpm ) );
    SetIcon( icon );

    m_hotkeysDescrList = g_Module_Viewer_Hotkeys_Descr;

    m_libList = new wxListBox( this, ID_MODVIEW_LIB_LIST,
            wxDefaultPosition, wxDefaultSize, 0, NULL, wxLB_HSCROLL );

    m_footprintList = new wxListBox( this, ID_MODVIEW_FOOTPRINT_LIST,
            wxDefaultPosition, wxDefaultSize, 0, NULL, wxLB_HSCROLL );

    SetBoard( new BOARD() );
    // In viewer, the default net clearance is not known (it depends on the actual board).
    // So we do not show the default clearance, by setting it to 0
    // The footprint or pad specific clearance will be shown
    GetBoard()->GetDesignSettings().GetDefault()->SetClearance(0);

    // Ensure all layers and items are visible:
    GetBoard()->SetVisibleAlls();
    SetScreen( new PCB_SCREEN( GetPageSizeIU() ) );

    GetScreen()->m_Center = true;      // Center coordinate origins on screen.
    LoadSettings( config() );
    GetGalDisplayOptions().m_axesEnabled = true;

    SetSize( m_FramePos.x, m_FramePos.y, m_FrameSize.x, m_FrameSize.y );

    GetScreen()->SetGrid( ID_POPUP_GRID_LEVEL_1000 + m_LastGridSizeId  );

    // Menu bar is not mandatory: uncomment/comment the next line
    // to add/remove the menubar
    ReCreateMenuBar();
    ReCreateHToolbar();
    ReCreateVToolbar();

    ReCreateLibraryList();
    UpdateTitle();

    // See for an existing board editor frame opened
    // (we need it just to know some settings )
    // TODO: find a better way to retrieve these settings)
    bool isBordEditorRunning = Kiway().Player( FRAME_PCB, false ) != nullptr;
    PCB_BASE_FRAME* pcbEditorFrame = static_cast<PCB_BASE_FRAME*>( Kiway().Player( FRAME_PCB, true ) );

    // Create GAL canvas
    PCB_DRAW_PANEL_GAL* drawPanel = new PCB_DRAW_PANEL_GAL( this, -1, wxPoint( 0, 0 ), m_FrameSize,
                                                            pcbEditorFrame->GetGalDisplayOptions(),
                                                            pcbEditorFrame->GetGalCanvas()->GetBackend() );
    SetGalCanvas( drawPanel );
    bool switchToGalCanvas = pcbEditorFrame->IsGalCanvasActive();

    // delete pcbEditorFrame if it was not yet in use:
    if( !isBordEditorRunning )
        pcbEditorFrame->Destroy();

    // Create the manager and dispatcher & route draw panel events to the dispatcher
    m_toolManager = new TOOL_MANAGER;
    m_toolManager->SetEnvironment( GetBoard(), drawPanel->GetView(),
                                   drawPanel->GetViewControls(), this );
    m_actions = new PCB_ACTIONS();
    m_toolDispatcher = new TOOL_DISPATCHER( m_toolManager, m_actions );
    drawPanel->SetEventDispatcher( m_toolDispatcher );

    m_toolManager->RegisterTool( new PCBNEW_CONTROL );
    m_toolManager->RegisterTool( new SELECTION_TOOL );  // for std context menus (zoom & grid)
    m_toolManager->RegisterTool( new COMMON_TOOLS );
    m_toolManager->InitTools();
    m_toolManager->InvokeTool( "pcbnew.InteractiveSelection" );

    // If a footprint was previously loaded, reload it
    if( getCurNickname().size() && getCurFootprintName().size() )
    {
        LIB_ID id;

        id.SetLibNickname( getCurNickname() );
        id.SetLibItemName( getCurFootprintName() );
        GetBoard()->Add( loadFootprint( id ) );
    }

    drawPanel->DisplayBoard( m_Pcb );

    m_auimgr.SetManagedWindow( this );

    wxSize minsize( 100, -1 );     // Min size of list boxes

    // Main toolbar is initially docked at the top of the main window and dockable on any side.
    // The close button is disable because the footprint viewer has no main menu to re-enable it.
    // The tool bar will only be dockable on the top or bottom of the main frame window.  This is
    // most likely due to the fact that the other windows are not dockable and are preventing the
    // tool bar from docking on the right and left.
    wxAuiPaneInfo toolbarPaneInfo;
    toolbarPaneInfo.Name( "m_mainToolBar" ).ToolbarPane().Top().CloseButton( false );

    EDA_PANEINFO info;
    info.InfoToolbarPane();

    EDA_PANEINFO mesg;
    mesg.MessageToolbarPane();

    // Manage main toolbar, top pane
    m_auimgr.AddPane( m_mainToolBar, toolbarPaneInfo );

    // Manage the list of libraries, left pane.
    m_auimgr.AddPane( m_libList,
                      wxAuiPaneInfo( info ).Name( "m_libList" )
                      .Left().Row( 1 ).MinSize( minsize ) );

    // Manage the list of footprints, center pane.
    m_auimgr.AddPane( m_footprintList,
                      wxAuiPaneInfo( info ).Name( "m_footprintList" )
                      .Left().Row( 2 ).MinSize( minsize ) );

    // Manage the draw panel, right pane.
    m_auimgr.AddPane( m_canvas,
                      wxAuiPaneInfo().Name( "DrawFrame" ).CentrePane() );
    m_auimgr.AddPane( (wxWindow*) GetGalCanvas(),
                      wxAuiPaneInfo().Name( "DrawFrameGal" ).CentrePane().Hide() );

    // Manage the message panel, bottom pane.
    m_auimgr.AddPane( m_messagePanel,
                      wxAuiPaneInfo( mesg ).Name( "MsgPanel" ).Bottom() );

    if( !m_perspective.IsEmpty() )
    {
        // Restore last saved sizes, pos and other params
        // However m_mainToolBar size cannot be set to its last saved size
        // because the actual size change depending on the way modview was called:
        // the tool to export the current footprint exist or not.
        // and the saved size is not always OK
        // the trick is to get the default toolbar size, and set the size after
        // calling LoadPerspective
        wxSize tbsize = m_mainToolBar->GetSize();
        m_auimgr.LoadPerspective( m_perspective, false );
        m_auimgr.GetPane( m_mainToolBar ).BestSize( tbsize );
    }

    // after changing something to the aui manager,
    // call Update()() to reflect the changes
    m_auimgr.Update();

    // Now Drawpanel is sized, we can use BestZoom to show the component (if any)
#ifdef USE_WX_GRAPHICS_CONTEXT
    GetScreen()->SetScalingFactor( BestZoom() );
#else
    Zoom_Automatique( false );
#endif

    GetGalCanvas()->GetGAL()->SetAxesEnabled( true );
    UseGalCanvas( switchToGalCanvas );
    updateView();

    if( !IsModal() )        // For modal mode, calling ShowModal() will show this frame
    {
        Raise();            // On some window managers, this is needed
        Show( true );
    }
}


FOOTPRINT_VIEWER_FRAME::~FOOTPRINT_VIEWER_FRAME()
{
}


void FOOTPRINT_VIEWER_FRAME::OnCloseWindow( wxCloseEvent& Event )
{
    DBG(printf( "%s:\n", __func__ );)

    // A workaround to avoid flicker, in modal mode when modview frame is destroyed,
    // when the aui toolbar is not docked (i.e. shown in a miniframe)
    // (usefull on windows only)
    m_mainToolBar->SetFocus();

    if( IsGalCanvasActive() )
        GetGalCanvas()->StopDrawing();

    if( IsModal() )
    {
        // Only dismiss a modal frame once, so that the return values set by
        // the prior DismissModal() are not bashed for ShowModal().
        if( !IsDismissed() )
            DismissModal( false );

        // window to be destroyed by the caller of KIWAY_PLAYER::ShowModal()
    }
    else
        Destroy();
}


void FOOTPRINT_VIEWER_FRAME::OnSize( wxSizeEvent& SizeEv )
{
    if( m_auimgr.GetManagedWindow() )
        m_auimgr.Update();

    SizeEv.Skip();
}


void FOOTPRINT_VIEWER_FRAME::OnSetRelativeOffset( wxCommandEvent& event )
{
    GetScreen()->m_O_Curseur = GetCrossHairPosition();
    UpdateStatusBar();
}


void FOOTPRINT_VIEWER_FRAME::ReCreateLibraryList()
{
    m_libList->Clear();

    std::vector< wxString > nicknames = Prj().PcbFootprintLibs()->GetLogicalLibs();

    for( unsigned ii = 0; ii < nicknames.size(); ii++ )
        m_libList->Append( nicknames[ii] );

    // Search for a previous selection:
    int index =  m_libList->FindString( getCurNickname() );

    if( index != wxNOT_FOUND )
    {
        m_libList->SetSelection( index, true );
    }
    else
    {
        // If not found, clear current library selection because it can be
        // deleted after a configuration change.
        setCurNickname( wxEmptyString );
        setCurFootprintName( wxEmptyString );
    }

    ReCreateFootprintList();
    ReCreateHToolbar();

    m_canvas->Refresh();
}


void FOOTPRINT_VIEWER_FRAME::ReCreateFootprintList()
{
    m_footprintList->Clear();

    if( !getCurNickname() )
    {
        setCurFootprintName( wxEmptyString );
        return;
    }

    auto fp_info_list = FOOTPRINT_LIST::GetInstance( Kiway() );

    wxString nickname = getCurNickname();

    fp_info_list->ReadFootprintFiles( Prj().PcbFootprintLibs(), !nickname ? NULL : &nickname );

    if( fp_info_list->GetErrorCount() )
    {
        fp_info_list->DisplayErrors( this );

        // For footprint libraries that support one footprint per file, there may have been
        // valid footprints read so show the footprints that loaded properly.
        if( fp_info_list->GetList().size() == 0 )
            return;
    }

    for( auto& footprint : fp_info_list->GetList() )
    {
        m_footprintList->Append( footprint->GetFootprintName() );
    }

    int index = m_footprintList->FindString( getCurFootprintName() );

    if( index == wxNOT_FOUND )
        setCurFootprintName( wxEmptyString );
    else
        m_footprintList->SetSelection( index, true );
}


void FOOTPRINT_VIEWER_FRAME::ClickOnLibList( wxCommandEvent& event )
{
    int ii = m_libList->GetSelection();

    if( ii < 0 )
        return;

    wxString name = m_libList->GetString( ii );

    if( getCurNickname() == name )
        return;

    setCurNickname( name );

    ReCreateFootprintList();
    UpdateTitle();
    ReCreateHToolbar();
}


void FOOTPRINT_VIEWER_FRAME::ClickOnFootprintList( wxCommandEvent& event )
{
    if( m_footprintList->GetCount() == 0 )
        return;

    int ii = m_footprintList->GetSelection();

    if( ii < 0 )
        return;

    wxString name = m_footprintList->GetString( ii );

    if( getCurFootprintName().CmpNoCase( name ) != 0 )
    {
        setCurFootprintName( name );

        // Delete the current footprint (MUST reset tools first)
        GetToolManager()->ResetTools( TOOL_BASE::MODEL_RELOAD );
        SetCurItem( nullptr );
        GetBoard()->m_Modules.DeleteAll();

        LIB_ID id;
        id.SetLibNickname( getCurNickname() );
        id.SetLibItemName( getCurFootprintName() );

        try
        {
            GetBoard()->Add( loadFootprint( id ) );
        }
        catch( const IO_ERROR& ioe )
        {
            wxString msg = wxString::Format(
                        _( "Could not load footprint \"%s\" from library \"%s\".\n\nError %s." ),
                        GetChars( getCurFootprintName() ),
                        GetChars( getCurNickname() ),
                        GetChars( ioe.What() ) );

            DisplayError( this, msg );
        }

        UpdateTitle();

        if( IsGalCanvasActive() )
            updateView();

        Zoom_Automatique( false );
        m_canvas->Refresh();
        Update3D_Frame();
    }
}


void FOOTPRINT_VIEWER_FRAME::DClickOnFootprintList( wxCommandEvent& event )
{
    if( IsModal() )
    {
        // @todo(DICK)
        ExportSelectedFootprint( event );

        // Prevent the double click from being as a single mouse button release
        // event in the parent window which would cause the part to be parked
        // rather than staying in move mode.
        // Remember the mouse button will be released in the parent window
        // thus creating a mouse button release event which should be ignored
        PCB_EDIT_FRAME* pcbframe = dynamic_cast<PCB_EDIT_FRAME*>( GetParent() );

        // The parent may not be the board editor:
        if( pcbframe )
        {
            pcbframe->SkipNextLeftButtonReleaseEvent();
        }
    }
}


void FOOTPRINT_VIEWER_FRAME::ExportSelectedFootprint( wxCommandEvent& event )
{
    int ii = m_footprintList->GetSelection();

    if( ii >= 0 )
    {
        wxString fp_name = m_footprintList->GetString( ii );

        LIB_ID fpid;

        fpid.SetLibNickname( getCurNickname() );
        fpid.SetLibItemName( fp_name );

        DismissModal( true, fpid.Format() );
    }
    else
    {
        DismissModal( false );
    }

    Close( true );
}


void FOOTPRINT_VIEWER_FRAME::LoadSettings( wxConfigBase* aCfg )
{
    EDA_DRAW_FRAME::LoadSettings( aCfg );
    m_configSettings.Load( aCfg );  // mainly, load the color config
}


void FOOTPRINT_VIEWER_FRAME::SaveSettings( wxConfigBase* aCfg )
{
    EDA_DRAW_FRAME::SaveSettings( aCfg );
}


const wxString FOOTPRINT_VIEWER_FRAME::getCurNickname()
{
    return Prj().GetRString( PROJECT::PCB_FOOTPRINT_VIEWER_NICKNAME );
}


void FOOTPRINT_VIEWER_FRAME::setCurNickname( const wxString& aNickname )
{
    Prj().SetRString( PROJECT::PCB_FOOTPRINT_VIEWER_NICKNAME, aNickname );
}


const wxString FOOTPRINT_VIEWER_FRAME::getCurFootprintName()
{
    return Prj().GetRString( PROJECT::PCB_FOOTPRINT_VIEWER_FPNAME );
}


void FOOTPRINT_VIEWER_FRAME::setCurFootprintName( const wxString& aName )
{
    Prj().SetRString( PROJECT::PCB_FOOTPRINT_VIEWER_FPNAME, aName );
}


void FOOTPRINT_VIEWER_FRAME::OnActivate( wxActivateEvent& event )
{
    EDA_DRAW_FRAME::OnActivate( event );

    // Ensure we do not have old selection:
    if( !event.GetActive() )
        return;

    // Ensure we have the right library list:
    std::vector< wxString > libNicknames = Prj().PcbFootprintLibs()->GetLogicalLibs();

    if( libNicknames.size() == m_libList->GetCount() )
    {
        unsigned ii;

        for( ii = 0;  ii < libNicknames.size();  ii++ )
        {
            if( libNicknames[ii] != m_libList->GetString( ii ) )
                break;
        }

        if( ii == libNicknames.size() )
            return;
    }

    // If we are here, the library list has changed, rebuild it
    ReCreateLibraryList();
    UpdateTitle();
}


bool FOOTPRINT_VIEWER_FRAME::ShowModal( wxString* aFootprint, wxWindow* aResultantFocusWindow )
{
    if( aFootprint && !aFootprint->IsEmpty() )
    {
        LIB_ID fpid;

        fpid.Parse( *aFootprint, LIB_ID::ID_PCB, true );

        if( fpid.IsValid() )
        {
            setCurNickname( fpid.GetLibNickname() );
            setCurFootprintName( fpid.GetLibItemName() );
            ReCreateFootprintList();
            SelectAndViewFootprint( NEW_PART );
        }
    }

    return KIWAY_PLAYER::ShowModal( aFootprint, aResultantFocusWindow );
}


bool FOOTPRINT_VIEWER_FRAME::GeneralControl( wxDC* aDC, const wxPoint& aPosition, EDA_KEY aHotKey )
{
    bool eventHandled = true;

    // Filter out the 'fake' mouse motion after a keyboard movement
    if( !aHotKey && m_movingCursorWithKeyboard )
    {
        m_movingCursorWithKeyboard = false;
        return false;
    }

    wxCommandEvent cmd( wxEVT_COMMAND_MENU_SELECTED );
    cmd.SetEventObject( this );

    wxPoint oldpos = GetCrossHairPosition();
    wxPoint pos = aPosition;
    GeneralControlKeyMovement( aHotKey, &pos, true );

    if( aHotKey )
    {
        eventHandled = OnHotKey( aDC, aHotKey, aPosition );
    }

    SetCrossHairPosition( pos );
    RefreshCrossHair( oldpos, aPosition, aDC );

    UpdateStatusBar();    // Display new cursor coordinates

    return eventHandled;
}


void FOOTPRINT_VIEWER_FRAME::Show3D_Frame( wxCommandEvent& event )
{
    EDA_3D_VIEWER* draw3DFrame = Get3DViewerFrame();

    // We can probably remove this for 6.0, but just to be safe we'll stick to
    // one 3DFrame at a time for 5.0
    if( draw3DFrame )
        draw3DFrame->Close( true );

    draw3DFrame = new EDA_3D_VIEWER( &Kiway(), this, _( "3D Viewer" ) );
    Update3D_Frame( false );

#ifdef  __WXMAC__
    // A stronger version of Raise() which promotes the window to its parent's level.
    draw3DFrame->ReparentQuasiModal();
#else
    draw3DFrame->Raise();     // Needed with some Window Managers
#endif

    draw3DFrame->Show( true );
}


void FOOTPRINT_VIEWER_FRAME::Update3D_Frame( bool aForceReloadFootprint )
{
    wxString title = wxString::Format( _( "3D Viewer" ) + wxT( " \u2014 %s" ),
                                       getCurFootprintName() );

    Update3DView( &title );
}


COLOR4D FOOTPRINT_VIEWER_FRAME::GetGridColor()
{
    return Settings().Colors().GetItemColor( LAYER_GRID );
}


void FOOTPRINT_VIEWER_FRAME::OnIterateFootprintList( wxCommandEvent& event )
{
    switch( event.GetId() )
    {
    case ID_MODVIEW_NEXT:
        SelectAndViewFootprint( NEXT_PART );
        break;

    case ID_MODVIEW_PREVIOUS:
        SelectAndViewFootprint( PREVIOUS_PART );
        break;

    default:
        wxString id = wxString::Format( "%i", event.GetId() );
        wxFAIL_MSG( "FOOTPRINT_VIEWER_FRAME::OnIterateFootprintList error: id = " + id );
    }
}


void FOOTPRINT_VIEWER_FRAME::OnLeftClick( wxDC* DC, const wxPoint& MousePos )
{
}


bool FOOTPRINT_VIEWER_FRAME::OnRightClick( const wxPoint& MousePos, wxMenu* PopMenu )
{
    return true;
}


void FOOTPRINT_VIEWER_FRAME::UpdateTitle()
{
    wxString title;
    wxString path;

    title.Printf( _( "Library Browser" ) + L" \u2014 %s",
            getCurNickname().size()
                ? getCurNickname()
                : _( "no library selected" ) );

    // Now, add the full path, for info
    if( getCurNickname().size() )
    {
        FP_LIB_TABLE* libtable = Prj().PcbFootprintLibs();
        const LIB_TABLE_ROW* row = libtable->FindRow( getCurNickname() );

        if( row )
            title << L" \u2014 " << row->GetFullURI( true );
    }

    SetTitle( title );
}


void FOOTPRINT_VIEWER_FRAME::SelectCurrentLibrary( wxCommandEvent& event )
{
    wxString selection = SelectLibrary( getCurNickname() );

    if( !!selection && selection != getCurNickname() )
    {
        setCurNickname( selection );

        UpdateTitle();
        ReCreateFootprintList();

        int id = m_libList->FindString( getCurNickname() );

        if( id >= 0 )
            m_libList->SetSelection( id );
    }
}


void FOOTPRINT_VIEWER_FRAME::SelectCurrentFootprint( wxCommandEvent& event )
{
    wxString curr_nickname = getCurNickname();
    MODULE*  oldmodule = GetBoard()->m_Modules;
    MODULE*  module = LoadModuleFromLibrary( curr_nickname, false );

    if( module )
    {
        // Only one footprint allowed: remove the previous footprint (if exists)
        if( oldmodule )
        {
            GetBoard()->Remove( oldmodule );
            delete oldmodule;
        }

        SetCrossHairPosition( wxPoint( 0, 0 ) );
        AddModuleToBoard( module );

        setCurFootprintName( module->GetFPID().GetLibItemName() );

        wxString nickname = module->GetFPID().GetLibNickname();

        if( !getCurNickname() && nickname.size() )
        {
            // Set the listbox
            int index =  m_libList->FindString( nickname );
            if( index != wxNOT_FOUND )
                m_libList->SetSelection( index, true );

            setCurNickname( nickname );
        }

        module->ClearFlags();
        SetCurItem( NULL );

        Zoom_Automatique( false );
        m_canvas->Refresh();
        Update3D_Frame();
        m_footprintList->SetStringSelection( getCurFootprintName() );
   }
}


void FOOTPRINT_VIEWER_FRAME::SelectAndViewFootprint( int aMode )
{
    if( !getCurNickname() )
        return;

    int selection = m_footprintList->FindString( getCurFootprintName() );

    if( aMode == NEXT_PART )
    {
        if( selection != wxNOT_FOUND && selection < (int)m_footprintList->GetCount()-1 )
            selection++;
    }

    if( aMode == PREVIOUS_PART )
    {
        if( selection != wxNOT_FOUND && selection > 0 )
            selection--;
    }

    if( selection != wxNOT_FOUND )
    {
        m_footprintList->SetSelection( selection );
        setCurFootprintName( m_footprintList->GetString( selection ) );
        SetCurItem( NULL );

        // Delete the current footprint
        GetBoard()->m_Modules.DeleteAll();

        MODULE* footprint = Prj().PcbFootprintLibs()->FootprintLoad(
                                getCurNickname(), getCurFootprintName() );

        if( footprint )
            GetBoard()->Add( footprint, ADD_APPEND );

        Update3D_Frame();

        if( IsGalCanvasActive() )
            updateView();
    }


    UpdateTitle();
    Zoom_Automatique( false );
    m_canvas->Refresh();
}


void FOOTPRINT_VIEWER_FRAME::RedrawActiveWindow( wxDC* DC, bool EraseBg )
{
    if( !GetBoard() )
        return;

    m_canvas->DrawBackGround( DC );
    GetBoard()->Draw( m_canvas, DC, GR_COPY );

    m_canvas->DrawCrossHair( DC );

    UpdateMsgPanel();
}


void FOOTPRINT_VIEWER_FRAME::UpdateMsgPanel()
{
    BOARD_ITEM* footprint = GetBoard()->m_Modules;

    if( footprint )
    {
        MSG_PANEL_ITEMS items;

        footprint->GetMsgPanelInfo( m_UserUnits, items );
        SetMsgPanel( items );
    }
    else
        ClearMsgPanel();
}


void FOOTPRINT_VIEWER_FRAME::updateView()
{
    if( IsGalCanvasActive() )
    {
        auto dp = static_cast<PCB_DRAW_PANEL_GAL*>( GetGalCanvas() );
        dp->UseColorScheme( &Settings().Colors() );
        dp->DisplayBoard( GetBoard() );
        m_toolManager->ResetTools( TOOL_BASE::MODEL_RELOAD );
        m_toolManager->RunAction( ACTIONS::zoomFitScreen, true );
        UpdateMsgPanel();
    }
}


void FOOTPRINT_VIEWER_FRAME::CloseFootprintViewer( wxCommandEvent& event )
{
    Close();
}
