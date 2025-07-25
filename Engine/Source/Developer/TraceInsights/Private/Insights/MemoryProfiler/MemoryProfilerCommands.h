// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/Commands.h"

class FMenuBuilder;

namespace UE::Insights::MemoryProfiler
{

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Class that holds all profiler commands.
 */
class FMemoryProfilerCommands : public TCommands<FMemoryProfilerCommands>
{
public:
	FMemoryProfilerCommands();
	virtual ~FMemoryProfilerCommands();
	virtual void RegisterCommands() override;

public:
	//////////////////////////////////////////////////
	// Global commands need to implement following method:
	//     void Map_<CommandName>_Global();
	// Custom commands needs to implement also the following method:
	//     const FUIAction <CommandName>_Custom(...) const;
	//////////////////////////////////////////////////

	/** Toggles visibility for the Timing view. Global and custom command. */
	TSharedPtr<FUICommandInfo> ToggleTimingViewVisibility;

	/** Toggles visibility for the Memory Investigation view. Global and custom command. */
	TSharedPtr<FUICommandInfo> ToggleMemInvestigationViewVisibility;

	/** Toggles visibility for the Memory Tags tree view. Global and custom command. */
	TSharedPtr<FUICommandInfo> ToggleMemTagTreeViewVisibility;

	/** Toggles visibility for the Modules view. Global and custom command. */
	TSharedPtr<FUICommandInfo> ToggleModulesViewVisibility;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Menu builder. Helper class for adding a customized menu entry using the global UI command info.
 */
class FMemoryProfilerMenuBuilder
{
public:
	/**
	 * Helper method for adding a customized menu entry using the global UI command info.
	 * FUICommandInfo cannot be executed with custom parameters, so we need to create a custom FUIAction,
	 * but sometime we have global and local version for the UI command, so reuse data from the global UI command info.
	 * Ex:
	 *     SessionInstance_ToggleCapture          - Global version will toggle capture process for all active session instances
	 *     SessionInstance_ToggleCapture_OneParam - Local version will toggle capture process only for the specified session instance
	 *
	 * @param MenuBuilder The menu to add items to
	 * @param FUICommandInfo A shared pointer to the UI command info
	 * @param UIAction Customized version of the UI command info stored in an UI action
	 */
	static void AddMenuEntry(FMenuBuilder& MenuBuilder, const TSharedPtr<FUICommandInfo>& UICommandInfo, const FUIAction& UIAction);
};

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Class that provides helper functions for the commands to avoid cluttering profiler manager with many small functions.
 * Can't contain any variables. Directly operates on the profiler manager instance.
 */
class FMemoryProfilerActionManager
{
	friend class FMemoryProfilerManager;

private:
	/** Private constructor. */
	FMemoryProfilerActionManager(class FMemoryProfilerManager* Instance)
		: This(Instance)
	{}

	//////////////////////////////////////////////////
	// Toggle Commands

#define DECLARE_TOGGLE_COMMAND(CmdName)\
public:\
	void Map_##CmdName##_Global(); /**< Maps UI command info CmdName with the specified UI command list. */\
	const FUIAction CmdName##_Custom(); /**< UI action for CmdName command. */\
protected:\
	void CmdName##_Execute(); /**< Handles FExecuteAction for CmdName. */\
	bool CmdName##_CanExecute() const; /**< Handles FCanExecuteAction for CmdName. */\
	ECheckBoxState CmdName##_GetCheckState() const; /**< Handles FGetActionCheckState for CmdName. */

	DECLARE_TOGGLE_COMMAND(ToggleTimingViewVisibility)
	DECLARE_TOGGLE_COMMAND(ToggleMemInvestigationViewVisibility)
	DECLARE_TOGGLE_COMMAND(ToggleMemTagTreeViewVisibility)
	DECLARE_TOGGLE_COMMAND(ToggleModulesViewVisibility)
#undef DECLARE_TOGGLE_COMMAND

	//////////////////////////////////////////////////

protected:
	/** Reference to the global instance of the profiler manager. */
	class FMemoryProfilerManager* This;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::MemoryProfiler
