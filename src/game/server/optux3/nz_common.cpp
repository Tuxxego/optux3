//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: A slow-moving, once-human headcrab victim with only melee attacks.
//
//=============================================================================//

#include "cbase.h"

#include "doors.h"

#include "simtimer.h"
#include "nz_base.h"
#include "ai_hull.h"
#include "ai_navigator.h"
#include "ai_memory.h"
#include "gib.h"
#include "soundenvelope.h"
#include "engine/IEngineSound.h"
#include "ammodef.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// ACT_FLINCH_PHYSICS


ConVar	sk_nz_health( "sk_nz_health","0");

envelopePoint_t envNaziZombieMoanVolumeFast[] =
{
	{	7.0f, 7.0f,
		0.1f, 0.1f,
	},
	{	0.0f, 0.0f,
		0.2f, 0.3f,
	},
};

envelopePoint_t envNaziZombieMoanVolume[] =
{
	{	1.0f, 1.0f,
		0.1f, 0.1f,
	},
	{	1.0f, 1.0f,
		0.2f, 0.2f,
	},
	{	0.0f, 0.0f,
		0.3f, 0.4f,
	},
};

envelopePoint_t envNaziZombieMoanVolumeLong[] =
{
	{	1.0f, 1.0f,
		0.3f, 0.5f,
	},
	{	1.0f, 1.0f,
		0.6f, 1.0f,
	},
	{	0.0f, 0.0f,
		0.3f, 0.4f,
	},
};

envelopePoint_t envNaziZombieMoanIgnited[] =
{
	{	1.0f, 1.0f,
		0.5f, 1.0f,
	},
	{	1.0f, 1.0f,
		30.0f, 30.0f,
	},
	{	0.0f, 0.0f,
		0.5f, 1.0f,
	},
};


//=============================================================================
//=============================================================================

class CNaziZombie : public CAI_BlendingHost<CNPC_NZBase>
{
	DECLARE_DATADESC();
	DECLARE_CLASS( CNaziZombie, CAI_BlendingHost<CNPC_NZBase> );

public:
	CNaziZombie()
	 : m_DurationDoorBash( 2, 6),
	   m_NextTimeToStartDoorBash( 3.0 )
	{
	}

	void Spawn( void );
	void Precache( void );

	void SetNaziZombieModel( void );
	void MoanSound( envelopePoint_t *pEnvelope, int iEnvelopeSize );
	bool ShouldBecomeTorso( const CTakeDamageInfo &info, float flDamageThreshold );
	bool CanBecomeLiveTorso() { return !m_fIsHeadless; }

	void GatherConditions( void );

	int SelectFailSchedule( int failedSchedule, int failedTask, AI_TaskFailureCode_t taskFailCode );
	int TranslateSchedule( int scheduleType );

#ifndef HL2_EPISODIC
	void CheckFlinches() {} // NaziZombie has custom flinch code
#endif // HL2_EPISODIC

	Activity NPC_TranslateActivity( Activity newActivity );

	void OnStateChange( NPC_STATE OldState, NPC_STATE NewState );

	void StartTask( const Task_t *pTask );
	void RunTask( const Task_t *pTask );

	virtual const char *GetLegsModel( void );
	virtual const char *GetTorsoModel( void );
	virtual const char *GetHeadcrabClassname( void );
	virtual const char *GetHeadcrabModel( void );

	virtual bool OnObstructingDoor( AILocalMoveGoal_t *pMoveGoal, 
								 CBaseDoor *pDoor,
								 float distClear, 
								 AIMoveResult_t *pResult );

	Activity SelectDoorBash();

	void Ignite( float flFlameLifetime, bool bNPCOnly = true, float flSize = 0.0f, bool bCalledByLevelDesigner = false );
	void Extinguish();
	int OnTakeDamage_Alive( const CTakeDamageInfo &inputInfo );
	bool IsHeavyDamage( const CTakeDamageInfo &info );
	bool IsSquashed( const CTakeDamageInfo &info );
	void BuildScheduleTestBits( void );

	void PrescheduleThink( void );
	int SelectSchedule ( void );

	void PainSound( const CTakeDamageInfo &info );
	void DeathSound( const CTakeDamageInfo &info );
	void AlertSound( void );
	void IdleSound( void );
	void AttackSound( void );
	void AttackHitSound( void );
	void AttackMissSound( void );
	void FootstepSound( bool fRightFoot );
	void FootscuffSound( bool fRightFoot );

	const char *GetMoanSound( int nSound );
	
public:
	DEFINE_CUSTOM_AI;

protected:
	static const char *pMoanSounds[];


private:
	CHandle< CBaseDoor > m_hBlockingDoor;
	float				 m_flDoorBashYaw;
	
	CRandSimTimer 		 m_DurationDoorBash;
	CSimTimer 	  		 m_NextTimeToStartDoorBash;

	Vector				 m_vPositionCharged;
};

LINK_ENTITY_TO_CLASS( nz_common, CNaziZombie );
LINK_ENTITY_TO_CLASS( nz_common_torso, CNaziZombie );

//---------------------------------------------------------
//---------------------------------------------------------
const char *CNaziZombie::pMoanSounds[] =
{
	 "NPC_BaseNaziZombie.Moan1",
	 "NPC_BaseNaziZombie.Moan2",
	 "NPC_BaseNaziZombie.Moan3",
	 "NPC_BaseNaziZombie.Moan4",
};

//=========================================================
// Conditions
//=========================================================
enum
{
	COND_BLOCKED_BY_DOOR = LAST_BASE_NZ_CONDITION,
	COND_DOOR_OPENED,
	COND_NZ_CHARGE_TARGET_MOVED,
};

//=========================================================
// Schedules
//=========================================================
enum
{
	SCHED_NZ_BASH_DOOR = LAST_BASE_NZ_SCHEDULE,
	SCHED_NZ_WANDER_ANGRILY,
	SCHED_NZ_CHARGE_ENEMY,
	SCHED_NZ_FAIL,
};

//=========================================================
// Tasks
//=========================================================
enum
{
	TASK_NZ_EXPRESS_ANGER = LAST_BASE_NZ_TASK,
	TASK_NZ_YAW_TO_DOOR,
	TASK_NZ_ATTACK_DOOR,
	TASK_NZ_CHARGE_ENEMY,
};

//-----------------------------------------------------------------------------

int ACT_NZ_TANTRUM;
int ACT_NZ_WALLPOUND;

BEGIN_DATADESC( CNaziZombie )

	DEFINE_FIELD( m_hBlockingDoor, FIELD_EHANDLE ),
	DEFINE_FIELD( m_flDoorBashYaw, FIELD_FLOAT ),
	DEFINE_EMBEDDED( m_DurationDoorBash ),
	DEFINE_EMBEDDED( m_NextTimeToStartDoorBash ),
	DEFINE_FIELD( m_vPositionCharged, FIELD_POSITION_VECTOR ),

END_DATADESC()


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNaziZombie::Precache( void )
{
	BaseClass::Precache();

	PrecacheModel( "models/zombie/classic.mdl" );
	PrecacheModel( "models/zombie/classic_torso.mdl" );
	PrecacheModel( "models/zombie/classic_legs.mdl" );

	PrecacheScriptSound( "NaziZombie.FootstepRight" );
	PrecacheScriptSound( "NaziZombie.FootstepLeft" );
	PrecacheScriptSound( "NaziZombie.FootstepLeft" );
	PrecacheScriptSound( "NaziZombie.ScuffRight" );
	PrecacheScriptSound( "NaziZombie.ScuffLeft" );
	PrecacheScriptSound( "NaziZombie.AttackHit" );
	PrecacheScriptSound( "NaziZombie.AttackMiss" );
	PrecacheScriptSound( "NaziZombie.Pain" );
	PrecacheScriptSound( "NaziZombie.Die" );
	PrecacheScriptSound( "NaziZombie.Alert" );
	PrecacheScriptSound( "NaziZombie.Idle" );
	PrecacheScriptSound( "NaziZombie.Attack" );

	PrecacheScriptSound( "NPC_BaseNaziZombie.Moan1" );
	PrecacheScriptSound( "NPC_BaseNaziZombie.Moan2" );
	PrecacheScriptSound( "NPC_BaseNaziZombie.Moan3" );
	PrecacheScriptSound( "NPC_BaseNaziZombie.Moan4" );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNaziZombie::Spawn( void )
{
	Precache();

	if( FClassnameIs( this, "nz_common" ) )
	{
		m_fIsTorso = false;
	}
	else
	{
		// This was placed as an nz_common_torso
		m_fIsTorso = true;
	}

	m_fIsHeadless = false;

#ifdef HL2_EPISODIC
	SetBloodColor( BLOOD_COLOR_RED );
#else
	SetBloodColor( BLOOD_COLOR_RED );
#endif // HL2_EPISODIC

	m_iHealth			= sk_nz_health.GetFloat();
	m_flFieldOfView		= 0.2;

	CapabilitiesClear();

	//GetNavigator()->SetRememberStaleNodes( false );

	BaseClass::Spawn();

	m_flNextMoanSound = gpGlobals->curtime + random->RandomFloat( 1.0, 4.0 );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNaziZombie::PrescheduleThink( void )
{
  	if( gpGlobals->curtime > m_flNextMoanSound )
  	{
  		if( CanPlayMoanSound() )
  		{
			// Classic guy idles instead of moans.
			IdleSound();

  			m_flNextMoanSound = gpGlobals->curtime + random->RandomFloat( 2.0, 5.0 );
  		}
  		else
 		{
  			m_flNextMoanSound = gpGlobals->curtime + random->RandomFloat( 1.0, 2.0 );
  		}
  	}

	BaseClass::PrescheduleThink();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNaziZombie::SelectSchedule ( void )
{
	if( HasCondition( COND_PHYSICS_DAMAGE ) && !m_ActBusyBehavior.IsActive() )
	{
		return SCHED_FLINCH_PHYSICS;
	}

	return BaseClass::SelectSchedule();
}

//-----------------------------------------------------------------------------
// Purpose: Sound of a footstep
//-----------------------------------------------------------------------------
void CNaziZombie::FootstepSound( bool fRightFoot )
{
	if( fRightFoot )
	{
		EmitSound(  "NaziZombie.FootstepRight" );
	}
	else
	{
		EmitSound( "NaziZombie.FootstepLeft" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Sound of a foot sliding/scraping
//-----------------------------------------------------------------------------
void CNaziZombie::FootscuffSound( bool fRightFoot )
{
	if( fRightFoot )
	{
		EmitSound( "NaziZombie.ScuffRight" );
	}
	else
	{
		EmitSound( "NaziZombie.ScuffLeft" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Play a random attack hit sound
//-----------------------------------------------------------------------------
void CNaziZombie::AttackHitSound( void )
{
	EmitSound( "NaziZombie.AttackHit" );
}

//-----------------------------------------------------------------------------
// Purpose: Play a random attack miss sound
//-----------------------------------------------------------------------------
void CNaziZombie::AttackMissSound( void )
{
	// Play a random attack miss sound
	EmitSound( "NaziZombie.AttackMiss" );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNaziZombie::PainSound( const CTakeDamageInfo &info )
{
	// We're constantly taking damage when we are on fire. Don't make all those noises!
	if ( IsOnFire() )
	{
		return;
	}

	EmitSound( "NaziZombie.Pain" );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNaziZombie::DeathSound( const CTakeDamageInfo &info ) 
{
	EmitSound( "NaziZombie.Die" );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNaziZombie::AlertSound( void )
{
	EmitSound( "NaziZombie.Alert" );

	// Don't let a moan sound cut off the alert sound.
	m_flNextMoanSound += random->RandomFloat( 2.0, 4.0 );
}

//-----------------------------------------------------------------------------
// Purpose: Returns a moan sound for this class of nz.
//-----------------------------------------------------------------------------
const char *CNaziZombie::GetMoanSound( int nSound )
{
	return pMoanSounds[ nSound % ARRAYSIZE( pMoanSounds ) ];
}

//-----------------------------------------------------------------------------
// Purpose: Play a random idle sound.
//-----------------------------------------------------------------------------
void CNaziZombie::IdleSound( void )
{
	if( GetState() == NPC_STATE_IDLE && random->RandomFloat( 0, 1 ) == 0 )
	{
		// Moan infrequently in IDLE state.
		return;
	}

	if( IsSlumped() )
	{
		// Sleeping nzs are quiet.
		return;
	}

	EmitSound( "NaziZombie.Idle" );
	MakeAISpookySound( 360.0f );
}

//-----------------------------------------------------------------------------
// Purpose: Play a random attack sound.
//-----------------------------------------------------------------------------
void CNaziZombie::AttackSound( void )
{
	EmitSound( "NaziZombie.Attack" );
}

//-----------------------------------------------------------------------------
// Purpose: Returns the classname (ie "npc_headcrab") to spawn when our headcrab bails.
//-----------------------------------------------------------------------------
const char *CNaziZombie::GetHeadcrabClassname( void )
{
	return "npc_headcrab";
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
const char *CNaziZombie::GetHeadcrabModel( void )
{
	return "models/headcrabclassic.mdl";
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char *CNaziZombie::GetLegsModel( void )
{
	return "models/zombie/classic_legs.mdl";
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
const char *CNaziZombie::GetTorsoModel( void )
{
	return "models/zombie/classic_torso.mdl";
}


//---------------------------------------------------------
//---------------------------------------------------------
void CNaziZombie::SetNaziZombieModel( void )
{
	Hull_t lastHull = GetHullType();

	if ( m_fIsTorso )
	{
		SetModel( "models/zombie/classic_torso.mdl" );
		SetHullType( HULL_TINY );
	}
	else
	{
		SetModel( "models/zombie/classic.mdl" );
		SetHullType( HULL_HUMAN );
	}

	SetBodygroup( NZ_BODYGROUP_HEADCRAB, !m_fIsHeadless );

	SetHullSizeNormal( true );
	SetDefaultEyeOffset();
	SetActivity( ACT_IDLE );

	// hull changed size, notify vphysics
	// UNDONE: Solve this generally, systematically so other
	// NPCs can change size
	if ( lastHull != GetHullType() )
	{
		if ( VPhysicsGetObject() )
		{
			SetupVPhysicsHull();
		}
	}
}

//---------------------------------------------------------
// Classic nz only uses moan sound if on fire.
//---------------------------------------------------------
void CNaziZombie::MoanSound( envelopePoint_t *pEnvelope, int iEnvelopeSize )
{
	if( IsOnFire() )
	{
		BaseClass::MoanSound( pEnvelope, iEnvelopeSize );
	}
}

//---------------------------------------------------------
//---------------------------------------------------------
bool CNaziZombie::ShouldBecomeTorso( const CTakeDamageInfo &info, float flDamageThreshold )
{
	if( IsSlumped() ) 
	{
		// Never break apart a slouched nz. This is because the most fun
		// slouched nzs to kill are ones sleeping leaning against explosive
		// barrels. If you break them in half in the blast, the force of being
		// so close to the explosion makes the body pieces fly at ridiculous 
		// velocities because the pieces weigh less than the whole.
		return false;
	}

	return BaseClass::ShouldBecomeTorso( info, flDamageThreshold );
}

//---------------------------------------------------------
//---------------------------------------------------------
void CNaziZombie::GatherConditions( void )
{
	BaseClass::GatherConditions();

	static int conditionsToClear[] = 
	{
		COND_BLOCKED_BY_DOOR,
		COND_DOOR_OPENED,
		COND_NZ_CHARGE_TARGET_MOVED,
	};

	ClearConditions( conditionsToClear, ARRAYSIZE( conditionsToClear ) );

	if ( m_hBlockingDoor == NULL || 
		 ( m_hBlockingDoor->m_toggle_state == TS_AT_TOP || 
		   m_hBlockingDoor->m_toggle_state == TS_GOING_UP )  )
	{
		ClearCondition( COND_BLOCKED_BY_DOOR );
		if ( m_hBlockingDoor != NULL )
		{
			SetCondition( COND_DOOR_OPENED );
			m_hBlockingDoor = NULL;
		}
	}
	else
		SetCondition( COND_BLOCKED_BY_DOOR );

	if ( ConditionInterruptsCurSchedule( COND_NZ_CHARGE_TARGET_MOVED ) )
	{
		if ( GetNavigator()->IsGoalActive() )
		{
			const float CHARGE_RESET_TOLERANCE = 60.0;
			if ( !GetEnemy() ||
				 ( m_vPositionCharged - GetEnemyLKP()  ).Length() > CHARGE_RESET_TOLERANCE )
			{
				SetCondition( COND_NZ_CHARGE_TARGET_MOVED );
			}
				 
		}
	}
}

//---------------------------------------------------------
//---------------------------------------------------------

int CNaziZombie::SelectFailSchedule( int failedSchedule, int failedTask, AI_TaskFailureCode_t taskFailCode )
{
	if ( HasCondition( COND_BLOCKED_BY_DOOR ) && m_hBlockingDoor != NULL )
	{
		ClearCondition( COND_BLOCKED_BY_DOOR );
		if ( m_NextTimeToStartDoorBash.Expired() && failedSchedule != SCHED_NZ_BASH_DOOR )
			return SCHED_NZ_BASH_DOOR;
		m_hBlockingDoor = NULL;
	}

	if ( failedSchedule != SCHED_NZ_CHARGE_ENEMY && 
		 IsPathTaskFailure( taskFailCode ) &&
		 random->RandomInt( 1, 100 ) < 50 )
	{
		return SCHED_NZ_CHARGE_ENEMY;
	}

	if ( failedSchedule != SCHED_NZ_WANDER_ANGRILY &&
		 ( failedSchedule == SCHED_TAKE_COVER_FROM_ENEMY || 
		   failedSchedule == SCHED_CHASE_ENEMY_FAILED ) )
	{
		return SCHED_NZ_WANDER_ANGRILY;
	}

	return BaseClass::SelectFailSchedule( failedSchedule, failedTask, taskFailCode );
}

//---------------------------------------------------------
//---------------------------------------------------------

int CNaziZombie::TranslateSchedule( int scheduleType )
{
	if ( scheduleType == SCHED_COMBAT_FACE && IsUnreachable( GetEnemy() ) )
		return SCHED_TAKE_COVER_FROM_ENEMY;

	if ( !m_fIsTorso && scheduleType == SCHED_FAIL )
		return SCHED_NZ_FAIL;

	return BaseClass::TranslateSchedule( scheduleType );
}

//---------------------------------------------------------

Activity CNaziZombie::NPC_TranslateActivity( Activity newActivity )
{
	newActivity = BaseClass::NPC_TranslateActivity( newActivity );

	if ( newActivity == ACT_RUN )
		return ACT_WALK;
		
	if ( m_fIsTorso && ( newActivity == ACT_NZ_TANTRUM ) )
		return ACT_IDLE;

	return newActivity;
}

//---------------------------------------------------------
//---------------------------------------------------------
void CNaziZombie::OnStateChange( NPC_STATE OldState, NPC_STATE NewState )
{
	BaseClass::OnStateChange( OldState, NewState );
}

//---------------------------------------------------------
//---------------------------------------------------------

void CNaziZombie::StartTask( const Task_t *pTask )
{
	switch( pTask->iTask )
	{
	case TASK_NZ_EXPRESS_ANGER:
		{
			if ( random->RandomInt( 1, 4 ) == 2 )
			{
				SetIdealActivity( (Activity)ACT_NZ_TANTRUM );
			}
			else
			{
				TaskComplete();
			}

			break;
		}

	case TASK_NZ_YAW_TO_DOOR:
		{
			AssertMsg( m_hBlockingDoor != NULL, "Expected condition handling to break schedule before landing here" );
			if ( m_hBlockingDoor != NULL )
			{
				GetMotor()->SetIdealYaw( m_flDoorBashYaw );
			}
			TaskComplete();
			break;
		}

	case TASK_NZ_ATTACK_DOOR:
		{
		 	m_DurationDoorBash.Reset();
			SetIdealActivity( SelectDoorBash() );
			break;
		}

	case TASK_NZ_CHARGE_ENEMY:
		{
			if ( !GetEnemy() )
				TaskFail( FAIL_NO_ENEMY );
			else if ( GetNavigator()->SetVectorGoalFromTarget( GetEnemy()->GetLocalOrigin() ) )
			{
				m_vPositionCharged = GetEnemy()->GetLocalOrigin();
				TaskComplete();
			}
			else
				TaskFail( FAIL_NO_ROUTE );
			break;
		}

	default:
		BaseClass::StartTask( pTask );
		break;
	}
}

//---------------------------------------------------------
//---------------------------------------------------------

void CNaziZombie::RunTask( const Task_t *pTask )
{
	switch( pTask->iTask )
	{
	case TASK_NZ_ATTACK_DOOR:
		{
			if ( IsActivityFinished() )
			{
				if ( m_DurationDoorBash.Expired() )
				{
					TaskComplete();
					m_NextTimeToStartDoorBash.Reset();
				}
				else
					ResetIdealActivity( SelectDoorBash() );
			}
			break;
		}

	case TASK_NZ_CHARGE_ENEMY:
		{
			break;
		}

	case TASK_NZ_EXPRESS_ANGER:
		{
			if ( IsActivityFinished() )
			{
				TaskComplete();
			}
			break;
		}

	default:
		BaseClass::RunTask( pTask );
		break;
	}
}

//---------------------------------------------------------
//---------------------------------------------------------

bool CNaziZombie::OnObstructingDoor( AILocalMoveGoal_t *pMoveGoal, CBaseDoor *pDoor, 
							  float distClear, AIMoveResult_t *pResult )
{
	if ( BaseClass::OnObstructingDoor( pMoveGoal, pDoor, distClear, pResult ) )
	{
		if  ( IsMoveBlocked( *pResult ) && pMoveGoal->directTrace.vHitNormal != vec3_origin )
		{
			m_hBlockingDoor = pDoor;
			m_flDoorBashYaw = UTIL_VecToYaw( pMoveGoal->directTrace.vHitNormal * -1 );	
		}
		return true;
	}

	return false;
}

//---------------------------------------------------------
//---------------------------------------------------------

Activity CNaziZombie::SelectDoorBash()
{
	if ( random->RandomInt( 1, 3 ) == 1 )
		return ACT_MELEE_ATTACK1;
	return (Activity)ACT_NZ_WALLPOUND;
}

//---------------------------------------------------------
// NaziZombies should scream continuously while burning, so long
// as they are alive... but NOT IN GERMANY!
//---------------------------------------------------------
void CNaziZombie::Ignite( float flFlameLifetime, bool bNPCOnly, float flSize, bool bCalledByLevelDesigner )
{
 	if( !IsOnFire() && IsAlive() )
	{
		BaseClass::Ignite( flFlameLifetime, bNPCOnly, flSize, bCalledByLevelDesigner );

		if ( !UTIL_IsLowViolence() )
		{
			RemoveSpawnFlags( SF_NPC_GAG );

			MoanSound( envNaziZombieMoanIgnited, ARRAYSIZE( envNaziZombieMoanIgnited ) );

			if ( m_pMoanSound )
			{
				ENVELOPE_CONTROLLER.SoundChangePitch( m_pMoanSound, 120, 1.0 );
				ENVELOPE_CONTROLLER.SoundChangeVolume( m_pMoanSound, 1, 1.0 );
			}
		}
	}
}

//---------------------------------------------------------
// If a nz stops burning and hasn't died, quiet him down
//---------------------------------------------------------
void CNaziZombie::Extinguish()
{
	if( m_pMoanSound )
	{
		ENVELOPE_CONTROLLER.SoundChangeVolume( m_pMoanSound, 0, 2.0 );
		ENVELOPE_CONTROLLER.SoundChangePitch( m_pMoanSound, 100, 2.0 );
		m_flNextMoanSound = gpGlobals->curtime + random->RandomFloat( 2.0, 4.0 );
	}

	BaseClass::Extinguish();
}

//---------------------------------------------------------
//---------------------------------------------------------
int CNaziZombie::OnTakeDamage_Alive( const CTakeDamageInfo &inputInfo )
{
#ifndef HL2_EPISODIC
	if ( inputInfo.GetDamageType() & DMG_BUCKSHOT )
	{
		if( !m_fIsTorso && inputInfo.GetDamage() > (m_iMaxHealth/3) )
		{
			// Always flinch if damaged a lot by buckshot, even if not shot in the head.
			// The reason for making sure we did at least 1/3rd of the nz's max health
			// is so the nz doesn't flinch every time the odd shotgun pellet hits them,
			// and so the maximum number of times you'll see a nz flinch like this is 2.(sjb)
			AddGesture( ACT_GESTURE_FLINCH_HEAD );
		}
	}
#endif // HL2_EPISODIC

	return BaseClass::OnTakeDamage_Alive( inputInfo );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNaziZombie::IsHeavyDamage( const CTakeDamageInfo &info )
{
#ifdef HL2_EPISODIC
	if ( info.GetDamageType() & DMG_BUCKSHOT )
	{
		if ( !m_fIsTorso && info.GetDamage() > (m_iMaxHealth/3) )
			return true;
	}

	// Randomly treat all damage as heavy
	if ( info.GetDamageType() & (DMG_BULLET | DMG_BUCKSHOT) )
	{
		// Don't randomly flinch if I'm melee attacking
		if ( !HasCondition(COND_CAN_MELEE_ATTACK1) && (RandomFloat() > 0.5) )
		{
			// Randomly forget I've flinched, so that I'll be forced to play a big flinch
			// If this doesn't happen, it means I may not fully flinch if I recently flinched
			if ( RandomFloat() > 0.75 )
			{
				Forget(bits_MEMORY_FLINCHED);
			}

			return true;
		}
	}
#endif // HL2_EPISODIC

	return BaseClass::IsHeavyDamage(info);
}

//---------------------------------------------------------
//---------------------------------------------------------
#define NZ_SQUASH_MASS	300.0f  // Anything this heavy or heavier squashes a nz good. (show special fx)
bool CNaziZombie::IsSquashed( const CTakeDamageInfo &info )
{
	if( GetHealth() > 0 )
	{
		return false;
	}

	if( info.GetDamageType() & DMG_CRUSH )
	{
		IPhysicsObject *pCrusher = info.GetInflictor()->VPhysicsGetObject();
		if( pCrusher && pCrusher->GetMass() >= NZ_SQUASH_MASS && info.GetInflictor()->WorldSpaceCenter().z > EyePosition().z )
		{
			// This heuristic detects when a nz has been squashed from above by a heavy
			// item. Done specifically so we can add gore effects to Ravenholm cartraps.
			// The nz must take physics damage from a 300+kg object that is centered above its eyes (comes from above)
			return true;
		}
	}

	return false;
}

//---------------------------------------------------------
//---------------------------------------------------------
void CNaziZombie::BuildScheduleTestBits( void )
{
	BaseClass::BuildScheduleTestBits();

	if( !m_fIsTorso && !IsCurSchedule( SCHED_FLINCH_PHYSICS ) && !m_ActBusyBehavior.IsActive() )
	{
		SetCustomInterruptCondition( COND_PHYSICS_DAMAGE );
	}
}

	
//=============================================================================

AI_BEGIN_CUSTOM_NPC( nz_common, CNaziZombie )

	DECLARE_CONDITION( COND_BLOCKED_BY_DOOR )
	DECLARE_CONDITION( COND_DOOR_OPENED )
	DECLARE_CONDITION( COND_NZ_CHARGE_TARGET_MOVED )

	DECLARE_TASK( TASK_NZ_EXPRESS_ANGER )
	DECLARE_TASK( TASK_NZ_YAW_TO_DOOR )
	DECLARE_TASK( TASK_NZ_ATTACK_DOOR )
	DECLARE_TASK( TASK_NZ_CHARGE_ENEMY )
	
	DECLARE_ACTIVITY( ACT_NZ_TANTRUM );
	DECLARE_ACTIVITY( ACT_NZ_WALLPOUND );

	DEFINE_SCHEDULE
	( 
		SCHED_NZ_BASH_DOOR,

		"	Tasks"
		"		TASK_SET_ACTIVITY				ACTIVITY:ACT_NZ_TANTRUM"
		"		TASK_SET_FAIL_SCHEDULE			SCHEDULE:SCHED_TAKE_COVER_FROM_ENEMY"
		"		TASK_NZ_YAW_TO_DOOR			0"
		"		TASK_FACE_IDEAL					0"
		"		TASK_NZ_ATTACK_DOOR			0"
		""
		"	Interrupts"
		"		COND_NZ_RELEASECRAB"
		"		COND_ENEMY_DEAD"
		"		COND_NEW_ENEMY"
		"		COND_DOOR_OPENED"
	)

	DEFINE_SCHEDULE
	(
		SCHED_NZ_WANDER_ANGRILY,

		"	Tasks"
		"		TASK_WANDER						480240" // 48 units to 240 units.
		"		TASK_WALK_PATH					0"
		"		TASK_WAIT_FOR_MOVEMENT			4"
		""
		"	Interrupts"
		"		COND_NZ_RELEASECRAB"
		"		COND_ENEMY_DEAD"
		"		COND_NEW_ENEMY"
		"		COND_DOOR_OPENED"
	)

	DEFINE_SCHEDULE
	(
		SCHED_NZ_CHARGE_ENEMY,


		"	Tasks"
		"		TASK_NZ_CHARGE_ENEMY		0"
		"		TASK_WALK_PATH					0"
		"		TASK_WAIT_FOR_MOVEMENT			0"
		"		TASK_PLAY_SEQUENCE				ACTIVITY:ACT_NZ_TANTRUM" /* placeholder until frustration/rage/fence shake animation available */
		""
		"	Interrupts"
		"		COND_NZ_RELEASECRAB"
		"		COND_ENEMY_DEAD"
		"		COND_NEW_ENEMY"
		"		COND_DOOR_OPENED"
		"		COND_NZ_CHARGE_TARGET_MOVED"
	)

	DEFINE_SCHEDULE
	(
		SCHED_NZ_FAIL,

		"	Tasks"
		"		TASK_STOP_MOVING		0"
		"		TASK_SET_ACTIVITY		ACTIVITY:ACT_NZ_TANTRUM"
		"		TASK_WAIT				1"
		"		TASK_WAIT_PVS			0"
		""
		"	Interrupts"
		"		COND_CAN_RANGE_ATTACK1 "
		"		COND_CAN_RANGE_ATTACK2 "
		"		COND_CAN_MELEE_ATTACK1 "
		"		COND_CAN_MELEE_ATTACK2"
		"		COND_GIVE_WAY"
		"		COND_DOOR_OPENED"
	)

AI_END_CUSTOM_NPC()

//=============================================================================
