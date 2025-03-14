//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "hsmapoverview.h"
#include "hud.h"
#include "hl2_shareddefs.h"

#include <vgui/ILocalize.h>
#include <vgui/ISurface.h>
#include <filesystem.h>
#include "gamerules.h"
#include "hl2_gamerules.h" // ?
#include "c_team.h"
#include "c_playerresource.h"
#include "vtf/vtf.h"
#include "clientmode.h"
#include <vgui_controls/AnimationController.h>
#include "voice_status.h"
#include "IVEngine2\hud_radar_panel.h"

// Credit to Halo: Source for the code.

DECLARE_HUDELEMENT( CHSMapOverview )

extern ConVar overview_health;
extern ConVar overview_names;
extern ConVar overview_tracks;
extern ConVar overview_locked;
extern ConVar overview_alpha;
extern ConVar cl_radaralpha;
ConVar cl_radar_locked( "cl_radar_locked", "0", FCVAR_ARCHIVE, "Lock the angle of the radar display?" );

void PreferredOverviewModeChanged( IConVar *pConVar, const char *oldString, float flOldValue )
{
	ConVarRef var( pConVar );
	char cmd[32];
	V_snprintf( cmd, sizeof( cmd ), "overview_mode %d\n", var.GetInt() );
	engine->ClientCmd( cmd );
}
ConVar overview_preferred_mode( "overview_preferred_mode", "1", FCVAR_ARCHIVE, "Preferred overview mode", PreferredOverviewModeChanged );

ConVar overview_preferred_view_size( "overview_preferred_view_size", "600", FCVAR_ARCHIVE, "Preferred overview view size" );

#define DEATH_ICON_FADE (7.5f)
#define DEATH_ICON_DURATION (10.0f)
#define LAST_SEEN_ICON_DURATION (4.0f)
#define DIFFERENCE_THRESHOLD (100.0f)

// To make your own green radar file from the map overview file, turn this on, and include vtf.lib
#define no_GENERATE_RADAR_FILE

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
static int AdjustValue( int curValue, int targetValue, int amount )
{
	if ( curValue > targetValue )
	{
		curValue -= amount;

		if ( curValue < targetValue )
			curValue = targetValue;
	}
	else if ( curValue < targetValue )
	{
		curValue += amount;

		if ( curValue > targetValue )
			curValue = targetValue;
	}

	return curValue;
}

void CHSMapOverview::InitTeamColorsAndIcons()
{
	BaseClass::InitTeamColorsAndIcons();

	Q_memset( m_TeamIconsSelf, 0, sizeof(m_TeamIconsSelf) );
	Q_memset( m_TeamIconsDead, 0, sizeof(m_TeamIconsDead) );
	Q_memset( m_TeamIconsOffscreen, 0, sizeof(m_TeamIconsOffscreen) );
	Q_memset( m_TeamIconsDeadOffscreen, 0, sizeof(m_TeamIconsDeadOffscreen) );

	m_radioFlash = -1;
	m_radioFlashOffscreen = -1;
	m_radarTint = -1;
	m_playerFacing = -1;
	m_cameraIconFirst = -1;
	m_cameraIconThird = -1;
	m_cameraIconFree = -1;

	// Setup team red
	m_TeamColors[MAP_ICON_T] = COLOR_RED;
	m_TeamIcons[MAP_ICON_T] = AddIconTexture( "sprites/player_red_small" );
	m_TeamIconsSelf[MAP_ICON_T] = AddIconTexture( "sprites/player_red_self" );
	m_TeamIconsDead[MAP_ICON_T] = AddIconTexture( "sprites/player_red_dead" );
	m_TeamIconsOffscreen[MAP_ICON_T] = AddIconTexture( "sprites/player_red_offscreen" );
	m_TeamIconsDeadOffscreen[MAP_ICON_T] = AddIconTexture( "sprites/player_red_dead_offscreen" );

	// Setup team blue
	m_TeamColors[MAP_ICON_CT] = COLOR_BLUE;
	m_TeamIcons[MAP_ICON_CT] = AddIconTexture( "sprites/player_blue_small" );
	m_TeamIconsSelf[MAP_ICON_CT] = AddIconTexture( "sprites/player_blue_self" );
	m_TeamIconsDead[MAP_ICON_CT] = AddIconTexture( "sprites/player_blue_dead" );
	m_TeamIconsOffscreen[MAP_ICON_CT] = AddIconTexture( "sprites/player_blue_offscreen" );
	m_TeamIconsDeadOffscreen[MAP_ICON_CT] = AddIconTexture( "sprites/player_blue_dead_offscreen" );

	// Setup team other
	m_playerFacing = AddIconTexture( "sprites/player_tick" );
	m_cameraIconFirst = AddIconTexture( "sprites/spectator_eye" );
	m_cameraIconThird = AddIconTexture( "sprites/spectator_3rdcam" );
	m_cameraIconFree = AddIconTexture( "sprites/spectator_freecam" );

	m_radioFlash = AddIconTexture("sprites/player_radio_ring");
	m_radioFlashOffscreen = AddIconTexture("sprites/player_radio_ring_offscreen");

	m_radarTint = AddIconTexture("sprites/radar_trans");

}

//-----------------------------------------------------------------------------
void CHSMapOverview::ApplySchemeSettings(IScheme *scheme)
{
	BaseClass::ApplySchemeSettings( scheme );

	m_hIconFont = scheme->GetFont( "DefaultSmall", true );
}

//-----------------------------------------------------------------------------
void CHSMapOverview::Update( void )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();

	if ( !pPlayer )
		return;

	int team = pPlayer->GetTeamNumber();

	// if dead with fadetoblack on, we can't show anything
	if ( mp_fadetoblack.GetBool() && team > TEAM_SPECTATOR && !pPlayer->IsAlive() )
	{
		SetMode( MAP_MODE_OFF );
		return;
	}

	bool inRadarMode = (GetMode() == MAP_MODE_RADAR);
	int specmode = pPlayer->GetObserverMode();
	// if alive, we can only be in radar mode
	if( !inRadarMode  &&  pPlayer->IsAlive())
	{
		SetMode( MAP_MODE_RADAR );
		inRadarMode = true;
	}

	if( inRadarMode )
	{
		if( specmode > OBS_MODE_DEATHCAM )
		{
			// If fully dead, we don't want to be radar any more
			SetMode( m_playerPreferredMode );
			m_flChangeSpeed = 0;
		}
		else
		{
			SetFollowEntity(pPlayer->entindex());
			UpdatePlayers();
		}
	}

	BaseClass::Update();

	if ( GetSpectatorMode() == OBS_MODE_CHASE )
	{
		// Follow the local player in chase cam, so the map rotates using the local player's angles
		SetFollowEntity( pPlayer->entindex() );
	}
}

//-----------------------------------------------------------------------------
CHSMapOverview::HSMapPlayer_t* CHSMapOverview::GetHSInfoForPlayerIndex( int index )
{
	if ( index < 0 || index >= MAX_PLAYERS )
		return NULL;

	return &m_PlayersHSInfo[ index ];
}

//-----------------------------------------------------------------------------
CHSMapOverview::HSMapPlayer_t* CHSMapOverview::GetHSInfoForPlayer(MapPlayer_t *player)
{
	if( player == NULL )
		return NULL;

	for( int i = 0; i < MAX_PLAYERS; i++ )
	{
		if( &m_Players[i] == player )
			return &m_PlayersHSInfo[i];
	}

	return NULL;
}

//-----------------------------------------------------------------------------
#define TIME_SPOTS_STAY_SEEN (0.5f)
#define TIME_UNTIL_ENEMY_SEEN (0.5f)
// rules that define if you can see a player on the overview or not
bool CHSMapOverview::CanPlayerBeSeen( MapPlayer_t *player )
{
	C_BasePlayer *localPlayer = C_BasePlayer::GetLocalPlayer();

	if (!localPlayer || !player )
		return false;

	HSMapPlayer_t *csPlayer = GetHSInfoForPlayer(player);
		
	if ( !csPlayer )
		return false;

	if( GetMode() == MAP_MODE_RADAR )
	{
		// This level will be for all the RadarMode thinking.  Base class will be the old way for the other modes.
		float now = gpGlobals->curtime;

		if( player->position == Vector(0,0,0) )
			return false; // Invalid guy.

		// draw special icons if within time
		if ( csPlayer->overrideExpirationTime != -1  &&  csPlayer->overrideExpirationTime > gpGlobals->curtime )
			return true;

		// otherwise, not dead people
		if( player->health <= 0 )
			return false;

		if( localPlayer->GetTeamNumber() == player->team )
			return true;// always yes for teammates.

		// and a living enemy needs to have been seen recently, and have been for a while
		if( csPlayer->timeLastSeen != -1  
			&& ( now - csPlayer->timeLastSeen < TIME_SPOTS_STAY_SEEN ) 
			&& ( now - csPlayer->timeFirstSeen > TIME_UNTIL_ENEMY_SEEN )
			)
			return true;

		return false;
	}
	else if( player->health <= 0 )
	{
		// Have to be under the overriden icon time to draw when dead.
		if ( csPlayer->overrideExpirationTime == -1  ||  csPlayer->overrideExpirationTime <= gpGlobals->curtime )
			return false;
	}
	
	return BaseClass::CanPlayerBeSeen(player);
}

CHSMapOverview::CHSMapOverview( const char *pElementName ) : BaseClass( pElementName )
{
	m_nRadarMapTextureID = -1;

	g_pMapOverview = this;  // for cvars access etc

	// restore non-radar modes
	switch ( overview_preferred_mode.GetInt() )
	{
	case MAP_MODE_INSET:
		m_playerPreferredMode = MAP_MODE_INSET;
		break;

	case MAP_MODE_FULL:
		m_playerPreferredMode = MAP_MODE_FULL;
		break;

	default:
		m_playerPreferredMode = MAP_MODE_OFF;
		break;
	}
}

void CHSMapOverview::Init( void )
{
	BaseClass::Init();
}

CHSMapOverview::~CHSMapOverview()
{
	g_pMapOverview = NULL;

	//TODO release Textures ? clear lists
}

void CHSMapOverview::UpdatePlayers()
{
	if( !m_goalIconsLoaded )
		UpdateGoalIcons();

	UpdateFlashes();

	C_PlayerResource *pPR = (C_PlayerResource*)GameResources();
	if ( !pPR )
		return;

	float now = gpGlobals->curtime;

	CBasePlayer *localPlayer = C_BasePlayer::GetLocalPlayer();
	if( localPlayer == NULL )
		return;

	MapPlayer_t *localMapPlayer = GetPlayerByUserID(localPlayer->GetUserID());
	if( localMapPlayer == NULL )
		return;

	for ( int i = 1; i<= gpGlobals->maxClients; i++)
	{
		MapPlayer_t *player = &m_Players[i-1];
		HSMapPlayer_t *playerCS = GetHSInfoForPlayerIndex(i-1);

		if ( !playerCS )
			continue;

		// update from global player resources
		if ( pPR->IsConnected(i) )
		{
			player->health = pPR->GetHealth( i );

			if ( !pPR->IsAlive( i ) )
			{
				// Safety actually happens after a TKPunish.
				player->health = 0;
				playerCS->isDead = true;
			}

			if ( player->team != pPR->GetTeam( i ) )
			{
				player->team = pPR->GetTeam( i );

				if( player == localMapPlayer )
					player->icon = m_TeamIconsSelf[ GetIconNumberFromTeamNumber(player->team) ];
				else
					player->icon = m_TeamIcons[ GetIconNumberFromTeamNumber(player->team) ];

				player->color = m_TeamColors[ GetIconNumberFromTeamNumber(player->team) ];
			}
		}

		Vector position = player->position;
		QAngle angles = player->angle;
		C_BasePlayer *pPlayer = UTIL_PlayerByIndex( i );
		if ( pPlayer && !pPlayer->IsDormant() )
		{
			// update position of active players in our PVS
			position = pPlayer->EyePosition();
			angles = pPlayer->EyeAngles();

			SetPlayerPositions( i-1, position, angles );
		}
	}

	if ( GetMode() == MAP_MODE_RADAR )
	{
		// Check for teammates spotting enemy players
		for ( int i = 1; i<= gpGlobals->maxClients; ++i )
		{
			if ( !pPR->IsConnected(i) )
				continue;

			if ( !pPR->IsAlive(i) )
				continue;

			if ( pPR->GetTeam(i) == localMapPlayer->team )
				continue;

			if ( pPR->IsPlayerSpotted(i) )
			{
				SetPlayerSeen( i-1 );

				MapPlayer_t *baseEnemy = &m_Players[i-1];
				if( baseEnemy->health > 0 )
				{
					// They were just seen, so if they are alive get rid of their "last known" icon
					HSMapPlayer_t *csEnemy = GetHSInfoForPlayerIndex(i-1);

					if ( csEnemy )
					{
						csEnemy->overrideIcon = -1;
						csEnemy->overrideIconOffscreen = -1;
						csEnemy->overridePosition = Vector(0, 0, 0);
						csEnemy->overrideAngle = QAngle(0, 0, 0);
						csEnemy->overrideFadeTime = -1;
						csEnemy->overrideExpirationTime = -1;
					}
				}
			}
		}

		for( int i = 1; i<= gpGlobals->maxClients; i++ )
		{
			MapPlayer_t *player = &m_Players[i-1];
			HSMapPlayer_t *playerCS = GetHSInfoForPlayerIndex(i-1);
			C_BasePlayer *pPlayer = UTIL_PlayerByIndex( i );

			if ( !pPlayer || !playerCS )
				continue;

			float timeSinceLastSeen = now - playerCS->timeLastSeen;
			if( timeSinceLastSeen < 0.25f )
				continue;
			if( player->health <= 0 )
				continue;// We don't need to spot dead guys, since they always show
			if( player->team == localMapPlayer->team )
				continue;// We don't need to spot our own guys

			// Now that everyone has had a say on people they can see for us, go through and handle baddies that can no longer be seen.
			if( playerCS->timeLastSeen != now  &&  player->health > 0 )
			{
				// We are not seen now, but if we were seen recently (and for long enough),
				// put up a "last known" icon and clear timelastseen
				// if they are alive.  Death icon is more important, which is why the health check above.
				if( timeSinceLastSeen < 0.5f  && ( playerCS->timeLastSeen != -1 ) )
				{
					if( now - playerCS->timeFirstSeen > TIME_UNTIL_ENEMY_SEEN )
					{
						int normalIcon, offscreenIcon;
						float zDifference = 0;
						if( localPlayer )
						{	
							if( (localPlayer->GetObserverMode() != OBS_MODE_NONE) && localPlayer->GetObserverTarget() )
								zDifference = player->position.z - localPlayer->GetObserverTarget()->GetAbsOrigin().z;
							else
								zDifference = player->position.z - localPlayer->GetAbsOrigin().z;
						}

						if( zDifference > DIFFERENCE_THRESHOLD )
						{
							normalIcon = m_TeamIcons[ GetIconNumberFromTeamNumber(player->team) ];
							offscreenIcon = m_TeamIconsOffscreen[ GetIconNumberFromTeamNumber(player->team) ];
						}
						else if( zDifference < -DIFFERENCE_THRESHOLD )
						{
							normalIcon = m_TeamIcons[ GetIconNumberFromTeamNumber(player->team) ];
							offscreenIcon = m_TeamIconsOffscreen[ GetIconNumberFromTeamNumber(player->team) ];
						}
						else
						{
							normalIcon = m_TeamIcons[GetIconNumberFromTeamNumber(player->team)];
							offscreenIcon = m_TeamIconsOffscreen[GetIconNumberFromTeamNumber(player->team)];
						}

						playerCS->overrideIcon = normalIcon;
						playerCS->overrideIconOffscreen = offscreenIcon;
						playerCS->overridePosition = player->position;
						playerCS->overrideFadeTime = -1;
						playerCS->overrideExpirationTime = now + LAST_SEEN_ICON_DURATION;
						playerCS->overrideAngle = player->angle;
					}
					playerCS->timeLastSeen = -1;
					playerCS->timeFirstSeen = -1;
				}
			}
		}
	}
}

bool CHSMapOverview::ShouldDraw( void )
{
	int alpha = GetMasterAlpha();
	if( alpha == 0 )
		return false;// we have been set to fully transparent

	if( GetMode() == MAP_MODE_RADAR )
	{
		if ( (GET_HUDELEMENT( CHudRadarPanel ))->ShouldDraw() == false )
		{
			return false; 
		}

		// We have to be alive and not blind to draw in this mode.
		C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
		if( !pPlayer || pPlayer->GetObserverMode() == OBS_MODE_DEATHCAM ) 
		{
			return false;
		}
	}

	return BaseClass::ShouldDraw();
}

CHSMapOverview::MapPlayer_t* CHSMapOverview::GetPlayerByEntityID( int entityID )
{
	C_BasePlayer *realPlayer = UTIL_PlayerByIndex(entityID);

	if( realPlayer == NULL )
		return NULL;

	for (int i=0; i<MAX_PLAYERS; i++)
	{
		MapPlayer_t *player = &m_Players[i];

		if ( player->userid == realPlayer->GetUserID() )
			return player;
	}

	return NULL;
}

#define BORDER_WIDTH 4
bool CHSMapOverview::AdjustPointToPanel(Vector2D *pos)
{
	if( pos == NULL )
		return false;

	int mapInset = GetBorderSize();// This gives us the amount inside the panel that the background is drawing.
	if( mapInset != 0 )
		mapInset += BORDER_WIDTH; // And this gives us the border inside the map edge to give us room for offscreen icons.

	int x,y,w,t;

	//MapTpPanel has already offset the x and y.  That's why we use 0 for left and top.
	GetBounds( x,y,w,t );

	bool madeChange = false;
	if( pos->x < mapInset )
	{
		pos->x = mapInset;
		madeChange = true;
	}
	if( pos->x > w - mapInset )
	{
		pos->x = w - mapInset;
		madeChange = true;
	}
	if( pos->y < mapInset )
	{
		pos->y = mapInset;
		madeChange = true;
	}
	if( pos->y > t - mapInset )
	{
		pos->y = t - mapInset;
		madeChange = true;
	}

	return madeChange;
}

void CHSMapOverview::DrawMapTexture()
{
	int alpha = GetMasterAlpha();

	if( GetMode() == MAP_MODE_FULL )
		SetBgColor( Color(0,0,0,0) );// no background in big mode
	else
		SetBgColor( Color(0,0,0,alpha * 0.5) );

	int textureIDToUse = m_nMapTextureID;
	bool foundRadarVersion = false;
	if( m_nRadarMapTextureID != -1 && GetMode() == MAP_MODE_RADAR )
	{
		textureIDToUse = m_nRadarMapTextureID;
		foundRadarVersion = true;
	}

	int mapInset = GetBorderSize();
	int pwidth, pheight; 
	GetSize(pwidth, pheight);

	if ( textureIDToUse > 0 )
	{
		// We are drawing to the whole panel with a little border
		Vector2D panelTL = Vector2D( mapInset, mapInset );
		Vector2D panelTR = Vector2D( pwidth - mapInset, mapInset );
		Vector2D panelBR = Vector2D( pwidth - mapInset, pheight - mapInset );
		Vector2D panelBL = Vector2D( mapInset, pheight - mapInset );

		// So where are those four points on the great big map?
		Vector2D textureTL = PanelToMap( panelTL );// The top left corner of the display is where on the master map?
		textureTL /= OVERVIEW_MAP_SIZE;// Texture Vec2D is 0 to 1
		Vector2D textureTR = PanelToMap( panelTR );
		textureTR /= OVERVIEW_MAP_SIZE;
		Vector2D textureBR = PanelToMap( panelBR );
		textureBR /= OVERVIEW_MAP_SIZE;
		Vector2D textureBL = PanelToMap( panelBL );
		textureBL /= OVERVIEW_MAP_SIZE;

		Vertex_t points[4] =
		{
			// To draw a textured polygon, the first column is where you want to draw (to), and the second is what you want to draw (from).
			// We want to draw to the panel (pulled in for a border), and we want to draw the part of the map texture that should be seen.
			// First column is in panel coords, second column is in 0-1 texture coords
			Vertex_t( panelTL, textureTL ),
			Vertex_t( panelTR, textureTR ),
			Vertex_t( panelBR, textureBR ),
			Vertex_t( panelBL, textureBL )
		};

		surface()->DrawSetColor( 255, 255, 255, alpha );
		surface()->DrawSetTexture( textureIDToUse );
		surface()->DrawTexturedPolygon( 4, points );
	}

	// If we didn't find the greenscale version of the map, then at least do a tint.
	if( !foundRadarVersion && GetMode() == MAP_MODE_RADAR )
	{
		surface()->DrawSetColor( 0,255,0, alpha / 4 );
		surface()->DrawFilledRect( mapInset, mapInset, m_vSize.x - 1 - mapInset, m_vSize.y - 1 - mapInset );
	}
}

bool CHSMapOverview::DrawIconHS( int textureID, int offscreenTextureID, Vector pos, float scale, float angle, int alpha, bool allowRotation, const char *text, Color *textColor, float status, Color *statusColor )
{
	if( GetMode() == MAP_MODE_RADAR  &&  cl_radaralpha.GetInt() == 0 )
		return false;

	if( alpha <= 0 )
		return false;

	Vector2D pospanel = WorldToMap( pos );
	pospanel = MapToPanel( pospanel );

	int idToUse = textureID;
	float angleToUse = angle;

	Vector2D oldPos = pospanel;
	Vector2D adjustment(0,0);
	if( AdjustPointToPanel( &pospanel ) )
	{
		if( offscreenTextureID == -1 )
			return false; // Doesn't want to draw if off screen.

		// Move it in to on panel, and change the icon.
		idToUse = offscreenTextureID;
		// And point towards the original spot
		adjustment = Vector2D(pospanel.x - oldPos.x, pospanel.y - oldPos.y);
		QAngle adjustmentAngles;
		Vector adjustment3D(adjustment.x, -adjustment.y, 0); // Y gets flipped in WorldToMap
		VectorAngles(adjustment3D, adjustmentAngles) ;
		if( allowRotation )
		{
			// Some icons don't want to rotate even when off radar
			angleToUse = adjustmentAngles[YAW];

			// And the angle needs to be in world space, not panel space.
			if( m_bFollowAngle )
			{
				angleToUse += m_fViewAngle;
			}
			else 
			{
				if ( m_bRotateMap )
					angleToUse += 180.0f;
				else
					angleToUse += 90.0f;
			}
		}

		// Don't draw names for icons that are offscreen (bunches up and looks bad)
		text = NULL;
	}

	int d = GetPixelOffset( scale );

	Vector offset;

	offset.x = -scale;	offset.y = scale;
	VectorYawRotate( offset, angleToUse, offset );
	Vector2D pos1 = WorldToMap( pos + offset );
	Vector2D pos1Panel = MapToPanel(pos1);
	pos1Panel.x += adjustment.x;
	pos1Panel.y += adjustment.y;

	offset.x = scale;	offset.y = scale;
	VectorYawRotate( offset, angleToUse, offset );
	Vector2D pos2 = WorldToMap( pos + offset );
	Vector2D pos2Panel = MapToPanel(pos2);
	pos2Panel.x += adjustment.x;
	pos2Panel.y += adjustment.y;

	offset.x = scale;	offset.y = -scale;
	VectorYawRotate( offset, angleToUse, offset );
	Vector2D pos3 = WorldToMap( pos + offset );
	Vector2D pos3Panel = MapToPanel(pos3);
	pos3Panel.x += adjustment.x;
	pos3Panel.y += adjustment.y;

	offset.x = -scale;	offset.y = -scale;
	VectorYawRotate( offset, angleToUse, offset );
	Vector2D pos4 = WorldToMap( pos + offset );
	Vector2D pos4Panel = MapToPanel(pos4);
	pos4Panel.x += adjustment.x;
	pos4Panel.y += adjustment.y;

	Vertex_t points[4] =
	{
		Vertex_t( pos1Panel, Vector2D(0,0) ),
			Vertex_t( pos2Panel, Vector2D(1,0) ),
			Vertex_t( pos3Panel, Vector2D(1,1) ),
			Vertex_t( pos4Panel, Vector2D(0,1) )
	};

	surface()->DrawSetColor( 255, 255, 255, alpha );
	surface()->DrawSetTexture( idToUse );
	surface()->DrawTexturedPolygon( 4, points );

	pospanel.y += d + 4;

	if ( status >=0.0f  && status <= 1.0f && statusColor )
	{
		// health bar is 50x3 pixels
		surface()->DrawSetColor( 0,0,0,255 );
		surface()->DrawFilledRect( pospanel.x-d, pospanel.y-1, pospanel.x+d, pospanel.y+1 );

		int length = (float)(d*2)*status;
		surface()->DrawSetColor( statusColor->r(), statusColor->g(), statusColor->b(), 255 );
		surface()->DrawFilledRect( pospanel.x-d, pospanel.y-1, pospanel.x-d+length, pospanel.y+1 );

		pospanel.y += 3;
	}

	if ( text && textColor )
	{
		wchar_t iconText[ MAX_PLAYER_NAME_LENGTH*2 ];

		g_pVGuiLocalize->ConvertANSIToUnicode( text, iconText, sizeof( iconText ) );

		int wide, tall;
		surface()->GetTextSize( m_hIconFont, iconText, wide, tall );

		int x = pospanel.x-(wide/2);
		int y = pospanel.y;

		// draw black shadow text
		surface()->DrawSetTextColor( 0, 0, 0, 255 );
		surface()->DrawSetTextPos( x+1, y );
		surface()->DrawPrintText( iconText, wcslen(iconText) );

		// draw name in color 
		surface()->DrawSetTextColor( textColor->r(), textColor->g(), textColor->b(), 255 );
		surface()->DrawSetTextPos( x, y );
		surface()->DrawPrintText( iconText, wcslen(iconText) );
	}

	return true;
}

void CHSMapOverview::DrawMapPlayers()
{
	DrawGoalIcons();

	C_PlayerResource *pPR = (C_PlayerResource*)GameResources();
	surface()->DrawSetTextFont( m_hIconFont );

	Color colorGreen( 0, 255, 0, 255 );	// health bar color
	CBasePlayer *localPlayer = C_BasePlayer::GetLocalPlayer();

	for (int i=0; i < MAX_PLAYERS; i++)
	{
		int alpha = 255;
		MapPlayer_t *player = &m_Players[i];
		HSMapPlayer_t *playerCS = GetHSInfoForPlayerIndex(i);

		if ( !playerCS )
			continue;

		if ( !CanPlayerBeSeen( player ) )
			continue;

		float status = -1;
		const char *name = NULL;

		if ( m_bShowNames && CanPlayerNameBeSeen( player ) )
			name = player->name;

		if ( m_bShowHealth && CanPlayerHealthBeSeen( player ) )
			status = player->health/100.0f;


		// Now draw them
		if( playerCS->overrideExpirationTime > gpGlobals->curtime )// If dead, an X, if alive, an alpha'd normal icon
		{
			int alphaToUse = alpha;
			if( playerCS->overrideFadeTime != -1 && playerCS->overrideFadeTime <= gpGlobals->curtime )
			{
				// Fade linearly from fade start to disappear
				alphaToUse *= 1 - (float)(gpGlobals->curtime - playerCS->overrideFadeTime) / (float)(playerCS->overrideExpirationTime - playerCS->overrideFadeTime);
			}

			DrawIconHS( playerCS->overrideIcon, playerCS->overrideIconOffscreen, playerCS->overridePosition, m_flIconSize * 1.1f, GetViewAngle(), player->health > 0 ? alphaToUse / 2 : alphaToUse, true, name, &player->color, -1, &colorGreen );
			if( player->health > 0 )
				DrawIconHS( m_playerFacing, -1, playerCS->overridePosition, m_flIconSize * 1.1f, playerCS->overrideAngle[YAW], player->health > 0 ? alphaToUse / 2 : alphaToUse, true, name, &player->color, status, &colorGreen );
		}
		else
		{
			float zDifference = 0;
			if( localPlayer )
			{	
				if( (localPlayer->GetObserverMode() != OBS_MODE_NONE) && localPlayer->GetObserverTarget() )
					zDifference = player->position.z - localPlayer->GetObserverTarget()->GetAbsOrigin().z;
				else
					zDifference = player->position.z - localPlayer->GetAbsOrigin().z;
			}

			float sizeForRing = m_flIconSize * 1.4f;
			if( zDifference > DIFFERENCE_THRESHOLD )
			{
				// A dot above is bigger and a little fuzzy now.
				sizeForRing = m_flIconSize * 1.9f;
			}
			else if( zDifference < -DIFFERENCE_THRESHOLD )
			{
				// A dot below is smaller.
				sizeForRing = m_flIconSize * 1.0f;
			}

			bool showTalkRing = localPlayer ? (localPlayer->GetTeamNumber() == player->team) : false;
			if( localPlayer && localPlayer->GetTeamNumber() == TEAM_SPECTATOR )
				showTalkRing = true;

			if( showTalkRing && playerCS->currentFlashAlpha > 0 )// Flash type
			{
				// Make them flash a halo
				DrawIconHS(m_radioFlash, m_radioFlashOffscreen, player->position, sizeForRing, player->angle[YAW], playerCS->currentFlashAlpha);
			}
			else if( showTalkRing && pPR->IsAlive( i + 1 ) && GetClientVoiceMgr()->IsPlayerSpeaking( i + 1) ) // Or solid on type
			{
				// Make them show a halo
				DrawIconHS(m_radioFlash, m_radioFlashOffscreen, player->position, sizeForRing, player->angle[YAW], 255);
			}

			float sizeForPlayer = m_flIconSize * 1.1f;// The 1.1 is because the player dots are shrunken a little, so their facing pip can have some space to live
			if( zDifference > DIFFERENCE_THRESHOLD )
			{
				// A dot above is bigger and a little fuzzy now.
				sizeForPlayer = m_flIconSize * 1.6f;
				alpha *= 0.5f;
			}
			else if( zDifference < -DIFFERENCE_THRESHOLD )
			{
				// A dot below is smaller.
				sizeForPlayer = m_flIconSize * 0.7f;
			}

			int normalIcon, offscreenIcon;
			normalIcon = player->icon;
			offscreenIcon = m_TeamIconsOffscreen[GetIconNumberFromTeamNumber(player->team)];

			bool doingLocalPlayer = false;
			if( GetPlayerByUserID(localPlayer->GetUserID()) == player )
				doingLocalPlayer = true;

			float angleForPlayer = GetViewAngle();

			if( doingLocalPlayer )
			{
				sizeForPlayer *= 4.0f; // The self icon is really big since it has a camera view cone attached.
				angleForPlayer = player->angle[YAW];// And, the self icon now rotates, natch.
			}

			DrawIconHS( normalIcon, offscreenIcon, player->position, sizeForPlayer, angleForPlayer, alpha, true, name, &player->color, status, &colorGreen );
			if( !doingLocalPlayer )
			{
				// Draw the facing for everyone but the local player.
				if( player->health > 0 )
					DrawIconHS( m_playerFacing, -1, player->position, sizeForPlayer, player->angle[YAW], alpha, true, name, &player->color, status, &colorGreen );
			}
		}
	}
}

void CHSMapOverview::SetMap(const char * levelname)
{
	BaseClass::SetMap(levelname);

	int wide, tall;
	surface()->DrawGetTextureSize( m_nMapTextureID, wide, tall );
	if( wide == 0 && tall == 0 )
	{
		m_nMapTextureID = -1;
		m_nRadarMapTextureID = -1;
		return;// No map image, so no radar image
	}

	char radarFileName[MAX_PATH];
	Q_snprintf(radarFileName, MAX_PATH, "%s_radar", m_MapKeyValues->GetString("material"));
	m_nRadarMapTextureID = surface()->CreateNewTextureID();
	surface()->DrawSetTextureFile(m_nRadarMapTextureID, radarFileName, true, false);
	int radarWide = -1;
	int radarTall = -1;
	surface()->DrawGetTextureSize(m_nRadarMapTextureID, radarWide, radarTall);
	bool radarTextureFound = false;
	if( radarWide == wide  &&  radarTall == tall )
	{
		// Unbelievable that these is no failure return from SetTextureFile, and not
		// even a ValidTexture check on the ID.  So I can check if it is different from
		// the original.  It'll be a 32x32 default if not present.
		radarTextureFound = true;
	}

	if( !radarTextureFound )
	{
		if( !CreateRadarImage(m_MapKeyValues->GetString("material"), radarFileName) )
			m_nRadarMapTextureID = -1;
	}

	ClearGoalIcons();
}

bool CHSMapOverview::CreateRadarImage(const char *mapName, const char * radarFileName)
{
#ifdef GENERATE_RADAR_FILE
	char fullFileName[MAX_PATH];
	Q_snprintf(fullFileName, MAX_PATH, "materials/%s.vtf", mapName);
	char fullRadarFileName[MAX_PATH];
	Q_snprintf(fullRadarFileName, MAX_PATH, "materials/%s.vtf", radarFileName);

	// Not found, so try to make one
	FileHandle_t fp;
	fp = ::filesystem->Open( fullFileName, "rb" );
	if( !fp )
	{
		return false;
	}
	::filesystem->Seek( fp, 0, FILESYSTEM_SEEK_TAIL );
	int srcVTFLength = ::filesystem->Tell( fp );
	::filesystem->Seek( fp, 0, FILESYSTEM_SEEK_HEAD );

	CUtlBuffer buf;
	buf.EnsureCapacity( srcVTFLength );
	int overviewMapBytesRead = ::filesystem->Read( buf.Base(), srcVTFLength, fp );
	::filesystem->Close( fp );

	buf.SeekGet( CUtlBuffer::SEEK_HEAD, 0 );// Need to set these explicitly since ->Read goes straight to memory and skips them.
	buf.SeekPut( CUtlBuffer::SEEK_HEAD, overviewMapBytesRead );

	IVTFTexture *radarTexture = CreateVTFTexture();
	if (radarTexture->Unserialize(buf))
	{
		ImageFormat oldImageFormat = radarTexture->Format();
		radarTexture->ConvertImageFormat(IMAGE_FORMAT_RGBA8888, false);
		unsigned char *imageData = radarTexture->ImageData(0,0,0);
		int size = radarTexture->ComputeTotalSize(); // in bytes!
		unsigned char *pEnd = imageData + size;

		for( ; imageData < pEnd; imageData += 4 )
		{
			imageData[0] = 0; // R
			imageData[2] = 0; // B
		}

		radarTexture->ConvertImageFormat(oldImageFormat, false);

		buf.Clear();
		radarTexture->Serialize(buf);

		fp = ::filesystem->Open(fullRadarFileName, "wb");
		::filesystem->Write(buf.Base(), buf.TellPut(), fp);
		::filesystem->Close(fp);
		DestroyVTFTexture(radarTexture);
		buf.Purge();

		// And need a vmt file to go with it.
		char vmtFilename[MAX_PATH];
		Q_snprintf(vmtFilename, MAX_PATH, "%s", fullRadarFileName);
		char *extension = &vmtFilename[Q_strlen(vmtFilename) - 3];
		*extension++ = 'v';
		*extension++ = 'm';
		*extension++ = 't';
		*extension++ = '\0';
		fp = ::filesystem->Open(vmtFilename, "wt");
		::filesystem->Write("\"UnlitGeneric\"\n", 15, fp);
		::filesystem->Write("{\n", 2, fp);
		::filesystem->Write("\t\"$translucent\" \"1\"\n", 20, fp);
		::filesystem->Write("\t\"$basetexture\" \"", 17, fp);
		::filesystem->Write(radarFileName, Q_strlen(radarFileName), fp);
		::filesystem->Write("\"\n", 2, fp);
		::filesystem->Write("\t\"$vertexalpha\" \"1\"\n", 20, fp);
		::filesystem->Write("\t\"$vertexcolor\" \"1\"\n", 20, fp);
		::filesystem->Write("\t\"$no_fullbright\" \"1\"\n", 22, fp);
		::filesystem->Write("\t\"$ignorez\" \"1\"\n", 16, fp);
		::filesystem->Write("}\n", 2, fp);
		::filesystem->Close(fp);

		m_nRadarMapTextureID = surface()->CreateNewTextureID();
		surface()->DrawSetTextureFile( m_nRadarMapTextureID, radarFileName, true, true);
		return true;
	}
#endif
	return false;
}

void CHSMapOverview::ResetRound()
{
	BaseClass::ResetRound();

	for (int i=0; i<MAX_PLAYERS; i++)
	{
		HSMapPlayer_t *p = &m_PlayersHSInfo[i];

		p->isDead = false;

		p->overrideFadeTime = -1;
		p->overrideExpirationTime = -1;
		p->overrideIcon = -1;
		p->overrideIconOffscreen = -1;
		p->overridePosition = Vector( 0, 0, 0);
		p->overrideAngle = QAngle(0, 0, 0);

		p->timeLastSeen = -1;
		p->timeFirstSeen = -1;

		p->flashUntilTime = -1;
		p->nextFlashPeakTime = -1;
		p->currentFlashAlpha = 0;
	}

	m_goalIconsLoaded = false;
}

void CHSMapOverview::DrawCamera()
{
	C_BasePlayer *localPlayer = C_BasePlayer::GetLocalPlayer();

	if (!localPlayer)
		return;

	if( localPlayer->GetObserverMode() == OBS_MODE_ROAMING )
	{
		// Instead of the programmer-art red dot, we'll draw an icon for when our camera is roaming.
		int alpha = 255;
		DrawIconHS(m_cameraIconFree, m_cameraIconFree, localPlayer->GetAbsOrigin(), m_flIconSize * 3.0f, localPlayer->EyeAngles()[YAW], alpha);
	}
	else if( localPlayer->GetObserverMode() == OBS_MODE_IN_EYE )
	{
		if( localPlayer->GetObserverTarget() )
		{
			// Fade it if it is on top of a player dot.  And don't rotate it.
			int alpha = 255 * 0.5f;
			DrawIconHS(m_cameraIconFirst, m_cameraIconFirst, localPlayer->GetObserverTarget()->GetAbsOrigin(), m_flIconSize * 1.5f, GetViewAngle(), alpha);
		}
	}
	else if( localPlayer->GetObserverMode() == OBS_MODE_CHASE )
	{
		if( localPlayer->GetObserverTarget() )
		{
			// Or Draw the third-camera a little bigger. (Needs room to be off the dot being followed)
			int alpha = 255;
			DrawIconHS(m_cameraIconThird, m_cameraIconThird, localPlayer->GetObserverTarget()->GetAbsOrigin(), m_flIconSize * 3.0f, localPlayer->EyeAngles()[YAW], alpha);
		}
	}
}

void CHSMapOverview::FireGameEvent( IGameEvent *event )
{
	const char * type = event->GetName();

	if ( Q_strcmp(type,"player_death") == 0 )
	{
		MapPlayer_t *player = GetPlayerByUserID( event->GetInt("userid") );

		if ( !player )
			return;

		player->health = 0;
		Q_memset( player->trail, 0, sizeof(player->trail) ); // clear trails

		HSMapPlayer_t *playerCS = GetHSInfoForPlayer(player);

		if ( !playerCS )
			return;

		playerCS->isDead = true;
		playerCS->overrideIcon = m_TeamIconsDead[GetIconNumberFromTeamNumber(player->team)];
		playerCS->overrideIconOffscreen = playerCS->overrideIcon;
		playerCS->overridePosition = player->position;
		playerCS->overrideAngle = player->angle;
		playerCS->overrideFadeTime = gpGlobals->curtime + DEATH_ICON_FADE;
		playerCS->overrideExpirationTime = gpGlobals->curtime + DEATH_ICON_DURATION;
	}
	else if ( Q_strcmp(type,"player_team") == 0 )
	{
		MapPlayer_t *player = GetPlayerByUserID( event->GetInt("userid") );

		if ( !player )
			return;

		CBasePlayer *localPlayer = C_BasePlayer::GetLocalPlayer();
		if( localPlayer == NULL )
			return;
		MapPlayer_t *localMapPlayer = GetPlayerByUserID(localPlayer->GetUserID());

		player->team = event->GetInt("team");

		if( player == localMapPlayer )
			player->icon = m_TeamIconsSelf[ GetIconNumberFromTeamNumber(player->team) ];
		else
			player->icon = m_TeamIcons[ GetIconNumberFromTeamNumber(player->team) ];

		player->color = m_TeamColors[ GetIconNumberFromTeamNumber(player->team) ];
	}
	else
	{
		BaseClass::FireGameEvent(event);
	}
}

void CHSMapOverview::SetMode(int mode)
{
	if ( mode == MAP_MODE_RADAR )
	{
		m_flChangeSpeed = 0; // change size instantly
		// We want the _output_ of the radar to be consistant, so we need to take the map scale in to account.
		float desiredZoom = (DESIRED_RADAR_RESOLUTION * m_fMapScale) / (OVERVIEW_MAP_SIZE * m_fFullZoom);

		g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "zoom", desiredZoom, 0.0, 0, AnimationController::INTERPOLATOR_LINEAR );

		if( CBasePlayer::GetLocalPlayer() )
			SetFollowEntity( CBasePlayer::GetLocalPlayer()->entindex() );

		SetPaintBackgroundType( 2 );// rounded corners
		ShowPanel( true );
	}
	else if ( mode == MAP_MODE_INSET )
	{
		SetPaintBackgroundType( 2 );// rounded corners

		float desiredZoom = (overview_preferred_view_size.GetFloat() * m_fMapScale) / (OVERVIEW_MAP_SIZE * m_fFullZoom);

		g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "zoom", desiredZoom, 0.0f, 0.2f, AnimationController::INTERPOLATOR_LINEAR );
	}
	else 
	{
		SetPaintBackgroundType( 0 );// square corners

		float desiredZoom = 1.0f;

		g_pClientMode->GetViewportAnimationController()->RunAnimationCommand( this, "zoom", desiredZoom, 0.0f, 0.2f, AnimationController::INTERPOLATOR_LINEAR );
	}

	BaseClass::SetMode(mode);
}

void CHSMapOverview::UpdateSizeAndPosition()
{
	int x,y,w,h;

	surface()->GetScreenSize( w, h );

	switch( m_nMode )
	{
	case MAP_MODE_RADAR:
		{
			// To allow custom hud scripts to work, get our size from the HudRadar element that people already tweak.
			int x, y, w, t;
			(GET_HUDELEMENT( CHudRadarPanel ))->GetBounds(x, y, w, t);
			m_vPosition.x = x;
			m_vPosition.y = y;

			if ( g_pSpectatorGUI && g_pSpectatorGUI->IsVisible() )
			{
				m_vPosition.y += g_pSpectatorGUI->GetTopBarHeight();
			}

			m_vSize.x = w;
			m_vSize.y = w; // Intentionally not 't'.  We need to enforce square-ness to prevent people from seeing more of the map by fiddling their HudLayout
			break;
		}

	case MAP_MODE_INSET:
		{
			m_vPosition.x = XRES(16);
			m_vPosition.y = YRES(16);

			if ( g_pSpectatorGUI && g_pSpectatorGUI->IsVisible() )
			{
				m_vPosition.y += g_pSpectatorGUI->GetTopBarHeight();
			}

			m_vSize.x = w/4;
			m_vSize.y = m_vSize.x/1.333;
			break;
		}

	case MAP_MODE_FULL:
	default:
		{
			m_vSize.x = w;
			m_vSize.y = h;

			m_vPosition.x = 0;
			m_vPosition.y = 0;

			if ( g_pSpectatorGUI && g_pSpectatorGUI->IsVisible() )
			{
				m_vPosition.y += g_pSpectatorGUI->GetTopBarHeight();
				m_vSize.y -= g_pSpectatorGUI->GetTopBarHeight();
				m_vSize.y -= g_pSpectatorGUI->GetBottomBarHeight();
			}
			break;
		}
	}

	GetBounds( x,y,w,h );

	if ( m_flChangeSpeed > 0 )
	{
		// adjust slowly
		int pixels = m_flChangeSpeed * gpGlobals->frametime;
		x = AdjustValue( x, m_vPosition.x, pixels );
		y = AdjustValue( y, m_vPosition.y, pixels );
		w = AdjustValue( w, m_vSize.x, pixels );
		h = AdjustValue( h, m_vSize.y, pixels );
	}
	else
	{
		// set instantly
		x = m_vPosition.x;
		y = m_vPosition.y;
		w = m_vSize.x;
		h = m_vSize.y;
	}

	SetBounds( x,y,w,h );
}

void CHSMapOverview::SetPlayerSeen( int index )
{
	HSMapPlayer_t *pCS = GetHSInfoForPlayerIndex(index);

	float now = gpGlobals->curtime;

	if( pCS )
	{
		if( pCS->timeLastSeen == -1 )
			pCS->timeFirstSeen = now;

		pCS->timeLastSeen = now;
	}
}

void CHSMapOverview::FlashEntity( int entityID )
{
	MapPlayer_t *player = GetPlayerByEntityID(entityID);
	if( player == NULL )
		return;

	HSMapPlayer_t *playerCS = GetHSInfoForPlayer(player);

	if ( !playerCS )
		return;

	playerCS->flashUntilTime = gpGlobals->curtime + 2.0f;
	playerCS->currentFlashAlpha = 255;
	playerCS->nextFlashPeakTime = gpGlobals->curtime + 0.5f;
}

void CHSMapOverview::UpdateFlashes()
{
	float now = gpGlobals->curtime;
	for (int i=0; i<MAX_PLAYERS; i++)
	{
		HSMapPlayer_t *playerCS = GetHSInfoForPlayerIndex(i);
		if( playerCS->flashUntilTime <= now )
		{
			// Flashing over.
			playerCS->currentFlashAlpha = 0;
		}
		else
		{
			if( playerCS->nextFlashPeakTime <= now )
			{
				// Time for a peak
				playerCS->currentFlashAlpha = 255;
				playerCS->nextFlashPeakTime = now + 0.5f;
				playerCS->nextFlashPeakTime = min( playerCS->nextFlashPeakTime, playerCS->flashUntilTime );
			}
			else
			{
				// Just fade away
				playerCS->currentFlashAlpha -= ((playerCS->currentFlashAlpha * gpGlobals->frametime) / (playerCS->nextFlashPeakTime - now));
				playerCS->currentFlashAlpha = max( 0, playerCS->currentFlashAlpha );
			}
		}
	}
}


//-----------------------------------------------------------------------------
void CHSMapOverview::SetPlayerPreferredMode( int mode )
{
	// A player has given an explicit overview_mode command, so we need to honor that when we are done being the radar.
	m_playerPreferredMode = mode;

	// save off non-radar preferred modes
	switch ( mode )
	{
	case MAP_MODE_OFF:
		overview_preferred_mode.SetValue( MAP_MODE_OFF );
		break;

	case MAP_MODE_INSET:
		overview_preferred_mode.SetValue( MAP_MODE_INSET );
		break;

	case MAP_MODE_FULL:
		overview_preferred_mode.SetValue( MAP_MODE_FULL );
		break;
	}
}

//-----------------------------------------------------------------------------
void CHSMapOverview::SetPlayerPreferredViewSize( float viewSize )
{
	overview_preferred_view_size.SetValue( viewSize );
}


//-----------------------------------------------------------------------------
int CHSMapOverview::GetIconNumberFromTeamNumber( int teamNumber )
{
	switch(teamNumber) 
	{
	case TEAM_TERRORIST:
		return MAP_ICON_T;

	case TEAM_CT:
		return MAP_ICON_CT;

	default:
		return MAP_ICON_CT;
	}
}

//-----------------------------------------------------------------------------
void CHSMapOverview::ClearGoalIcons()
{
	m_goalIcons.RemoveAll();
}

//-----------------------------------------------------------------------------
void CHSMapOverview::UpdateGoalIcons()
{
	// The goal entities don't exist on the client, so we have to get them from the CS Resource.
	C_PlayerResource *pPR = (C_PlayerResource*)GameResources();
	if ( !pPR )
		return;
}

//-----------------------------------------------------------------------------
void CHSMapOverview::DrawGoalIcons()
{
	for( int iconIndex = 0; iconIndex < m_goalIcons.Count(); iconIndex++ )
	{
		// Goal icons are drawn without turning, but with edge adjustment.
		HSMapGoal_t *currentIcon = &(m_goalIcons[iconIndex]);
		int alpha = 255;
		DrawIconHS(currentIcon->iconToUse, currentIcon->iconToUse, currentIcon->position, m_flIconSize, GetViewAngle(), alpha, false);
	}
}

//-----------------------------------------------------------------------------
bool CHSMapOverview::IsRadarLocked() 
{
	return cl_radar_locked.GetBool();
}

//-----------------------------------------------------------------------------
int CHSMapOverview::GetMasterAlpha( void )
{
	// The master alpha is the alpha that the map wants to draw at.  The background will be at half that, and the icons
	// will always be full.  (The icons fade themselves for functional reasons like seen-recently.)
	int alpha = 255;
	if( GetMode() == MAP_MODE_RADAR )
		alpha = cl_radaralpha.GetInt();
	else
		alpha = overview_alpha.GetFloat() * 255;
	alpha = clamp( alpha, 0, 255 );

	return alpha;
}

//-----------------------------------------------------------------------------
int CHSMapOverview::GetBorderSize( void )
{
	switch( GetMode() )
	{
		case MAP_MODE_RADAR:
			return 4;
		case MAP_MODE_INSET:
			return 4;
		case MAP_MODE_FULL:
		default:
			return 0;
	}
}

//-----------------------------------------------------------------------------
Vector2D CHSMapOverview::PanelToMap( const Vector2D &panelPos )
{
	// This is the reversing of baseclass's MapToPanel
	int pwidth, pheight; 
	GetSize(pwidth, pheight);
	float viewAngle = GetViewAngle();
	float fScale = (m_fZoom * m_fFullZoom) / OVERVIEW_MAP_SIZE;

	Vector offset;
	offset.x = (panelPos.x - (pwidth * 0.5f)) / pheight;
	offset.y = (panelPos.y - (pheight * 0.5f)) / pheight;

	offset.x /= fScale;
	offset.y /= fScale;

	VectorYawRotate( offset, -viewAngle, offset );

	Vector2D mapPos;
	mapPos.x = offset.x + m_MapCenter.x;
	mapPos.y = offset.y + m_MapCenter.y;

	return mapPos;
}