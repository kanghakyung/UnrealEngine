// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Text;
using EpicGames.Core;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// Represents the FOptionalProperty engine type
	/// </summary>
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "OptionalProperty", IsProperty = true)]
	public class UhtOptionalProperty : UhtContainerBaseProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName => "OptionalProperty";

		/// <inheritdoc/>
		protected override string CppTypeText => "TOptional";

		/// <inheritdoc/>
		protected override string PGetMacroText => "TOPTIONAL";

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument => UhtPGetArgumentType.TypeText;

		/// <summary>
		/// Construct a new Optional property
		/// </summary>
		/// <param name="propertySettings">Property settings</param>
		/// <param name="value">Inner property value</param>
		public UhtOptionalProperty(UhtPropertySettings propertySettings, UhtProperty value) : base(propertySettings, value)
		{
			// If the creation of the value property set more flags, then copy those flags to ourselves
			PropertyFlags |= ValueProperty.PropertyFlags & (EPropertyFlags.UObjectWrapper | EPropertyFlags.TObjectPtr);

			if (ValueProperty.MetaData.ContainsKey(UhtNames.NativeConst))
			{
				MetaData.Add(UhtNames.NativeConstTemplateArg, "");
				ValueProperty.MetaData.Remove(UhtNames.NativeConst);
			}

			PropertyCaps |= UhtPropertyCaps.PassCppArgsByRef;
			PropertyCaps &= ~(UhtPropertyCaps.CanBeContainerKey);
			UpdateCaps();

			ValueProperty.SourceName = SourceName;
			ValueProperty.EngineName = EngineName;
			ValueProperty.PropertyFlags = (ValueProperty.PropertyFlags & EPropertyFlags.PropagateKeepInInner) | (PropertyFlags & EPropertyFlags.PropagateToOptionalInner);
			ValueProperty.Outer = this;
			ValueProperty.MetaData.Clear();
		}

		/// <inheritdoc/>
		protected override bool ResolveSelf(UhtResolvePhase phase)
		{
			bool results = base.ResolveSelf(phase);
			switch (phase)
			{
				case UhtResolvePhase.Final:
					PropertyFlags |= ResolveAndReturnNewFlags(ValueProperty, phase);
					MetaData.Add(ValueProperty.MetaData);
					ValueProperty.PropertyFlags = (ValueProperty.PropertyFlags & EPropertyFlags.PropagateKeepInInner) | (PropertyFlags & EPropertyFlags.PropagateToOptionalInner);
					ValueProperty.MetaData.Clear();
					PropagateFlagsFromInnerAndHandlePersistentInstanceMetadata(this, MetaData, ValueProperty);
					UpdateCaps();
					break;
			}
			return results;
		}

		private void UpdateCaps()
		{
			PropertyCaps &= ~(UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint | UhtPropertyCaps.CanExposeOnSpawn);
			PropertyCaps |= ValueProperty.PropertyCaps & UhtPropertyCaps.CanExposeOnSpawn;
			if (ValueProperty.PropertyCaps.HasAnyFlags(UhtPropertyCaps.IsParameterSupportedByBlueprint))
			{
				PropertyCaps |= UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
			}
		}

		/// <inheritdoc/>
		public override void CollectReferencesInternal(IUhtReferenceCollector collector, bool templateProperty)
		{
			ValueProperty.CollectReferencesInternal(collector, true);
		}

		/// <inheritdoc/>
		public override string? GetForwardDeclarations()
		{
			return ValueProperty.GetForwardDeclarations();
		}

		/// <inheritdoc/>
		public override IEnumerable<UhtType> EnumerateReferencedTypes()
		{
			foreach (UhtType type in ValueProperty.EnumerateReferencedTypes())
			{
				yield return type;
			}
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder builder, UhtPropertyTextType textType, bool isTemplateArgument)
		{
			switch (textType)
			{
				case UhtPropertyTextType.SparseShort:
					builder.Append("TOptional");
					break;

				case UhtPropertyTextType.FunctionThunkParameterArgType:
					builder.AppendFunctionThunkParameterArrayType(ValueProperty, true);
					break;

				case UhtPropertyTextType.VerseMangledType:
					builder.Append('?');
					builder.AppendPropertyVerseMangledType(ValueProperty);
					break;

				default:
					builder.Append("TOptional<").AppendPropertyText(ValueProperty, textType, true).Append('>');
					break;
			}
			return builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMetaDataDecl(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, int tabs)
		{
			builder.AppendMetaDataDecl(ValueProperty, context, name, GetNameSuffix(nameSuffix, "_Inner"), tabs);
			return base.AppendMetaDataDecl(builder, context, name, nameSuffix, tabs);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, int tabs)
		{
			builder.AppendMemberDecl(ValueProperty, context, name, GetNameSuffix(nameSuffix, "_Inner"), tabs);
			return AppendMemberDecl(builder, context, name, nameSuffix, tabs, "FGenericPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, string? offset, int tabs)
		{
			builder.AppendMemberDef(ValueProperty, context, name, GetNameSuffix(nameSuffix, "_Inner"), "0", tabs);
			AppendMemberDefStart(builder, context, name, nameSuffix, offset, tabs, "FGenericPropertyParams", "UECodeGen_Private::EPropertyGenFlags::Optional");
			AppendMemberDefEnd(builder, context, name, nameSuffix);
			return builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberPtr(StringBuilder builder, IUhtPropertyMemberContext context, string name, string nameSuffix, int tabs)
		{
			builder.AppendMemberPtr(ValueProperty, context, name, GetNameSuffix(nameSuffix, "_Inner"), tabs);
			base.AppendMemberPtr(builder, context, name, nameSuffix, tabs);
			return builder;
		}

		/// <inheritdoc/>
		public override void AppendObjectHashes(StringBuilder builder, int startingLength, IUhtPropertyMemberContext context)
		{
			ValueProperty.AppendObjectHashes(builder, startingLength, context);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder builder, bool isInitializer)
		{
			builder.AppendPropertyText(this, UhtPropertyTextType.Construction).Append("()");
			return builder;
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader defaultValueReader, StringBuilder innerDefaultValue)
		{
			return false;
		}

		/// <inheritdoc/>
		protected override void ValidateFunctionArgument(UhtFunction function, UhtValidationOptions options)
		{
			base.ValidateFunctionArgument(function, options);

			if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
			{
				if (!function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetRequest))
				{
					if (RefQualifier != UhtPropertyRefQualifier.ConstRef && !IsStaticArray)
					{
						this.LogError("Replicated TOptional parameters must be passed by const reference");
					}
				}
			}
		}

		/// <inheritdoc/>
		public override void Validate(UhtStruct outerStruct, UhtProperty outermostProperty, UhtValidationOptions options)
		{
			base.Validate(outerStruct, outermostProperty, options);

			if (PropertyFlags.HasAnyFlags(EPropertyFlags.Net))
			{
				this.LogError("Replicated Optionals with MemoryImageAllocators are not yet supported");
			}

			if (ValueProperty is UhtStructProperty structProperty)
			{
				if (structProperty.ScriptStruct == outerStruct)
				{
					this.LogError($"'Struct' recursion via optionals is unsupported for properties.");
				}
			}
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty other)
		{
			if (other is UhtOptionalProperty otherOptional)
			{
				return ValueProperty.IsSameType(otherOptional.ValueProperty);
			}
			return false;
		}

		#region Keyword
		[UhtPropertyType(Keyword = "TOptional")]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
		private static UhtProperty? OptionalProperty(UhtPropertyResolveArgs args)
		{
			UhtPropertySettings propertySettings = args.PropertySettings;
			IUhtTokenReader tokenReader = args.TokenReader;

			using UhtMessageContext tokenContext = new("TOptional");
			if (!args.SkipExpectedType())
			{
				return null;
			}
			tokenReader.Require('<');

			// Parse the value type
			UhtProperty? value = args.ParseTemplateParam(propertySettings.SourceName);
			if (value == null)
			{
				return null;
			}

			if (!value.PropertyCaps.HasAnyFlags(UhtPropertyCaps.CanBeOptionalValue))
			{
				tokenReader.LogError($"The type \'{value.GetUserFacingDecl()}\' can not be used as a value in a TOptional");
			}

			tokenReader.Require('>');

			//@TODO: Prevent sparse delegate types from being used in a container

			return new UhtOptionalProperty(propertySettings, value);
		}
		#endregion
	}
}
