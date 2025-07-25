// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StaticArray.h"
#include "MuR/Serialisation.h"

#include "Misc/TVariant.h"
#include "Math/Color.h"
#include "Math/Quat.h"
#include "Math/Vector.h"
#include "Math/IntVector.h"
#include "Math/Vector4.h"
#include "Math/Transform.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuR/MutableMath.h"
#include "MuR/Types.h"

#define UE_API MUTABLERUNTIME_API


namespace mu
{	

    //!
    class FInputMemoryStream : public FInputStream
    {
    public:

        //-----------------------------------------------------------------------------------------
        // Life cycle
        //-----------------------------------------------------------------------------------------

        //! Create the stream using an external buffer.
        //! The buffer will not be owned by this object, so it cannot be deallocated while this
        //! objects is in use.
        UE_API FInputMemoryStream( const void* pBuffer, uint64 size );


        //-----------------------------------------------------------------------------------------
        // FInputStream interface
        //-----------------------------------------------------------------------------------------
        UE_API void Read( void* pData, uint64 size ) override;


    private:

		const void* Buffer = nullptr;
		uint64 Size = 0;
		uint64 Pos = 0;

    };


#define MUTABLE_IMPLEMENT_POD_SERIALISABLE(Type)				     \
    void DLLEXPORT operator<<(FOutputArchive& Arch, const Type& T)   \
    {																 \
        Arch.Stream->Write(&T, sizeof(Type));						 \
    }																 \
                                                                     \
    void DLLEXPORT operator>>(FInputArchive& Arch, Type& T)		     \
    {																 \
        Arch.Stream->Read(&T, sizeof(Type));						 \
    }																 \
		

#define MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(Type)                        \
    template<class Alloc>                                                      \
	void DLLEXPORT operator<<(FOutputArchive& Arch, const TArray<Type, Alloc>& V)         \
	{                                                                          \
		uint32 Num = uint32(V.Num());                                          \
		Arch << Num;                                                           \
		if (Num)                                                               \
		{                                                                      \
			Arch.Stream->Write(V.GetData(), Num * sizeof(Type));			   \
		}                                                                      \
	}                                                                          \
                                                                               \
    template<class Alloc>                                                      \
	void DLLEXPORT operator>>(FInputArchive& Arch, TArray<Type, Alloc>& V)     \
	{                                                                          \
		uint32 Num;                                                            \
		Arch >> Num;                                                           \
		V.SetNum(Num);                                                         \
		if (Num)                                                               \
		{                                                                      \
			Arch.Stream->Read(V.GetData(), Num * sizeof(Type));					   \
		}                                                                      \
	}                                                                          \

	/** TVariant custom serialize. Based on the default serialization. */
	template <typename... Ts>
	void operator<<(FOutputArchive& Ar, const TVariant<Ts...>& Variant)
	{
		const uint8 Index = static_cast<uint8>(Variant.GetIndex());
		Ar << Index;
		
		Visit([&Ar](auto& StoredValue)
		{
			Ar << StoredValue;
		}, Variant);
	}


	template <typename T, typename VariantType>
	struct TVariantLoadFromInputArchiveCaller
	{
		/** Default construct the type and load it from the FArchive */
		static void Load(FInputArchive& Ar, VariantType& OutVariant)
		{
			OutVariant.template Emplace<T>();
			Ar >> OutVariant.template Get<T>();
		}
	};

	
	template <typename... Ts>
	struct TVariantLoadFromInputArchiveLookup
	{
		using VariantType = TVariant<Ts...>;
		static_assert((std::is_default_constructible<Ts>::value && ...), "Each type in TVariant template parameter pack must be default constructible in order to use FArchive serialization");

		/** Load the type at the specified index from the FArchive and emplace it into the TVariant */
		static void Load(SIZE_T TypeIndex, FInputArchive& Ar, VariantType& OutVariant)
		{
			static constexpr void(*Loaders[])(FInputArchive&, VariantType&) = { &TVariantLoadFromInputArchiveCaller<Ts, VariantType>::Load... };
			check(TypeIndex < UE_ARRAY_COUNT(Loaders));
			Loaders[TypeIndex](Ar, OutVariant);
		}
	};

	
	template <typename... Ts>
	void operator>>(FInputArchive& Ar, TVariant<Ts...>& Variant)
	{
		uint8 Index;
		Ar >> Index;
		check(Index < sizeof...(Ts));

		TVariantLoadFromInputArchiveLookup<Ts...>::Load(static_cast<SIZE_T>(Index), Ar, Variant);
	}

#define MUTABLE_IMPLEMENT_ENUM_SERIALISABLE(Type)						\
        void DLLEXPORT operator<<(FOutputArchive& Arch, const Type& T)   \
		{																\
            uint32 V = (uint32)T;                                       \
            Arch.Stream->Write(&V, sizeof(uint32));					\
		}																\
																		\
        void DLLEXPORT operator>>(FInputArchive& Arch, Type& T)   		\
		{																\
            uint32 V;													\
            Arch.Stream->Read(&V, sizeof(uint32));					\
			T = (Type)V;												\
		}																\

    template<typename T0, typename T1>
    inline void operator<<(FOutputArchive& Arch, const std::pair<T0, T1>& V)
    {
        Arch << V.first;
        Arch << V.second;
    }

    template<typename T0, typename T1>
    inline void operator>>(FInputArchive& Arch, std::pair<T0, T1>& V)
    {
        Arch >> V.first;
        Arch >> V.second;
    }
	
	template<typename T, uint32 Size, uint32 Align> 
    void operator<<(FOutputArchive& Arch, const TStaticArray<T, Size, Align>& V)
	{
		for (int32 I = 0; I < Size; ++I)
		{
			Arch << V[I];
		}
	}

	template<typename T, uint32 Size, uint32 Align> 
    void operator>>(FInputArchive& Arch, TStaticArray<T, Size, Align>& V)
	{
		for (uint32 I = 0; I < Size; ++I)
		{
			Arch >> V[I];
		}
	}
	

	//---------------------------------------------------------------------------------------------
	template< typename K, typename T >
	inline void operator<< (FOutputArchive& Arch, const TMap<K, T>& V)
	{
		Arch << (uint32)V.Num();
		for (const TPair<K, T>& Element : V)
		{
			Arch << Element.Key;
			Arch << Element.Value;
		}
	}

	template< typename K, typename T >
	inline void operator>> (FInputArchive& Arch, TMap<K, T>& V)
	{
		uint32 Num;
		Arch >> Num;

		for (uint32 Index = 0; Index < Num; ++Index)
		{
			K Key;
			T Element;
			Arch >> Key;
			Arch >> Element;

			V.Emplace(MoveTemp(Key), MoveTemp(Element));
		}
	}

	// Unreal POD Serializables
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(float);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(double);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(uint8);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(uint16);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(uint32);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(uint64);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(int8);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(int16);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(int32);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(int64);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(TCHAR);

	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(FIntVector2);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(FUintVector2);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(UE::Math::TIntVector2<uint16>);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(UE::Math::TIntVector2<int16>);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(FVector2f);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(FVector4f);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(FMatrix44f);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(FRichCurveKey);


	//---------------------------------------------------------------------------------------------
	template< typename T >
	inline void operator<< (FOutputArchive& Arch, const TSharedPtr<const T>& Ptr)
	{
		if (!Ptr)
		{
			Arch << (int32)-1;
		}
		else
		{
			int32* it = Arch.History.Find(Ptr.Get());

			if (!it)
			{
				int32 Id = Arch.History.Num();
				Arch.History.Add(Ptr.Get(), Id);
				Arch << Id;
				T::Serialise(Ptr.Get(), Arch);
			}
			else
			{
				Arch << *it;
			}
		}
	}

	template< typename T >
	inline void operator>> (FInputArchive& Arch, TSharedPtr<const T>& Ptr)
	{
		int32 Id;
		Arch >> Id;

		if (Id == -1)
		{
			Ptr.Reset();
		}
		else
		{
			if (Id < Arch.History.Num())
			{
				Ptr = StaticCastSharedPtr<T>(Arch.History[Id]);

				// If the pointer was null it means the position in history is used, but not set yet
				// option 1: we have a smart pointer loop which is very bad.
				// option 2: the resource in this Ptr is also pointed by a Proxy that has absorbed it
				//			 and this reference should also be a proxy instead of a pointer.
				check(Ptr);
			}
			else
			{
				// Ids come in order, but they may have been absorbed outside in some serialisations
				// like proxies.
				//check( Id == Arch.>History.Num() );
				Arch.History.SetNum(Id + 1, EAllowShrinking::No);

				TSharedPtr<T> Temp = T::StaticUnserialise(Arch);
				Ptr = Temp;
				Arch.History[Id] = Temp;
			}
		}
	}


	//---------------------------------------------------------------------------------------------
	template< typename T >
	inline void operator<< ( FOutputArchive& arch, const Ptr<T>& p )
	{
		operator<<( arch, (const Ptr<const T>&) p );
	}

	template< typename T >
	inline void operator>> ( FInputArchive& arch, Ptr<T>& p )
	{
		operator>>( arch, (Ptr<const T>&) p );
	}


	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	template<typename T0, typename T1> inline void operator<<(FOutputArchive& arch, const TPair<T0,T1>& v)
	{
		arch << v.Key;
		arch << v.Value;
	}

	template<typename T0, typename T1> inline void operator>>(FInputArchive& arch, TPair<T0, T1>& v)
	{
		arch >> v.Key;
		arch >> v.Value;
	}


	//---------------------------------------------------------------------------------------------
	// TODO: As POD?
	template< typename T >
	inline void operator<< (FOutputArchive& arch, const UE::Math::TQuat<T>& v)
	{		
		arch << v.X;
		arch << v.Y;
		arch << v.Z;
		arch << v.W;
	}

	template< typename T >
	inline void operator>> (FInputArchive& arch, UE::Math::TQuat<T>& v)
	{
		arch >> v.X;
		arch >> v.Y;
		arch >> v.Z;
		arch >> v.W;
	}


	//---------------------------------------------------------------------------------------------
	// TODO: As POD?
	template< typename T >
	inline void operator<< (FOutputArchive& arch, const UE::Math::TVector<T>& v)
	{
		arch << v.X;
		arch << v.Y;
		arch << v.Z;
	}

	template< typename T >
	inline void operator>> (FInputArchive& arch, UE::Math::TVector<T>& v)
	{
		arch >> v.X;
		arch >> v.Y;
		arch >> v.Z;
	}


	//---------------------------------------------------------------------------------------------
	template< typename T >
	inline void operator<< (FOutputArchive& arch, const UE::Math::TTransform<T>& v)
	{
		arch << v.GetRotation();
		arch << v.GetTranslation();
		arch << v.GetScale3D();
	}

	template< typename T >
	inline void operator>> (FInputArchive& arch, UE::Math::TTransform<T>& v)
	{
		UE::Math::TQuat<T> Rot;
		UE::Math::TVector<T> Trans;
		UE::Math::TVector<T> Scale;

		arch >> Rot;
		arch >> Trans;
		arch >> Scale;

		v.SetComponents(Rot, Trans, Scale);
	}


}

#undef UE_API
