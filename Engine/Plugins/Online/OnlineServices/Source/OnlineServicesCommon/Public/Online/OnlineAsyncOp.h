// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineAsyncOpHandle.h"
#include "Online/OnlineResult.h"
#include "Online/OnlineServicesCommonDelegates.h"
#include "Online/OnlineServicesDelegates.h"
#include "Online/OnlineServicesLog.h"
#include "Online/OnlineTypeInfo.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Traits/ElementType.h"

namespace UE::Online {

class FOnlineServicesCommon;

class FOnlineAsyncOp;
template <typename OpType> class TOnlineAsyncOp;
template <typename OpType, typename T> class TOnlineChainableAsyncOp;

enum class EOnlineAsyncExecutionPolicy : uint8
{
	RunOnGameThread,	// Run on the game thread, will execute immediately if we are already on the game thread
	RunOnNextTick,		// Run on the game thread next time we tick
	RunOnThreadPool,	// Run on a specified thread pool
	RunOnTaskGraph,		// Run on the task graph
	RunImmediately		// Call immediately, in the current thread
};

class FOnlineAsyncExecutionPolicy
{
public:
	FOnlineAsyncExecutionPolicy(EOnlineAsyncExecutionPolicy InExecutionPolicy)
		: ExecutionPolicy(InExecutionPolicy)
	{
	}

	static FOnlineAsyncExecutionPolicy RunOnGameThread() { return FOnlineAsyncExecutionPolicy(EOnlineAsyncExecutionPolicy::RunOnGameThread); }
	static FOnlineAsyncExecutionPolicy RunOnNextTick() { return FOnlineAsyncExecutionPolicy(EOnlineAsyncExecutionPolicy::RunOnNextTick); }
	static FOnlineAsyncExecutionPolicy RunOnThreadPool() { return FOnlineAsyncExecutionPolicy(EOnlineAsyncExecutionPolicy::RunOnThreadPool); } // TODO: allow thread pool to be specified
	static FOnlineAsyncExecutionPolicy RunOnTaskGraph() { return FOnlineAsyncExecutionPolicy(EOnlineAsyncExecutionPolicy::RunOnTaskGraph); }
	static FOnlineAsyncExecutionPolicy RunImmediately() { return FOnlineAsyncExecutionPolicy(EOnlineAsyncExecutionPolicy::RunImmediately); }

	const EOnlineAsyncExecutionPolicy& GetExecutionPolicy() const { return ExecutionPolicy; }

private:
	EOnlineAsyncExecutionPolicy ExecutionPolicy;
};

/* For use when we need to put a TOnlineResult in an object that needs to be default constructed such as a TPromise */
template <typename Result>
class TDefaultErrorResultInternal : public TResult<Result, FOnlineError>
{
public:
	using TResult<Result, FOnlineError>::TResult;

	TDefaultErrorResultInternal()
		: TResult<Result, FOnlineError>(Errors::Unknown())
	{
	}
};

template <typename OpType>
class TDefaultErrorResult : public TOnlineResult<OpType>
{
public:
	using TOnlineResult<OpType>::TOnlineResult;

	TDefaultErrorResult()
		: TOnlineResult<OpType>(Errors::Unknown())
	{
	}
};

template <typename TResultType>
struct TContinuationResult
{
public:
	static TContinuationResult Repeat() { return TContinuationResult<TResultType>(); }
	static TContinuationResult Complete(TResultType&& InResult) { return TContinuationResult(MoveTempIfPossible(InResult)); }
	bool IsComplete() const { return Result.IsSet(); }
	TResultType& GetResult() { return Result.GetValue(); }
	const TResultType& GetResult() const { return Result.GetValue(); }
private:

	TContinuationResult()
	{
	}
	TContinuationResult(TResultType&& InResult) : Result(InResult)
	{
	}
	TOptional<TResultType> Result;
};

template <>
struct TContinuationResult<void>
{
public:
	static TContinuationResult Repeat() { return TContinuationResult<void>(); }
	static TContinuationResult Complete() { return TContinuationResult(FIsCompleteTag()); }
	bool IsComplete() const { return bIsComplete; }
private:
	struct FIsCompleteTag {};

	TContinuationResult()
	{
	}
	TContinuationResult(const FIsCompleteTag&) : bIsComplete(true)
	{
	}
	bool bIsComplete = false;
};

template <typename T>
struct TContinuationResultType
{
	using ResultType = T;
};

template <typename T>
struct TContinuationResultType<TContinuationResult<T>>
{
	using ResultType = T;
};

template <typename T>
using TContinuationResultType_T = typename TContinuationResultType<T>::ResultType;

template <typename T>
FString ToLogString(const TDefaultErrorResultInternal<T>& Result)
{
	if (Result.IsOk())
	{
		return ToLogString(Result.GetOkValue());
	}
	else
	{
		return ToLogString(Result.GetErrorValue());
	}
}

namespace Private
{

class FOnlineOperationData // Map of (TypeName,Key)->(data of any Type)
{
public:
	template <typename T>
	void Set(const FString& Key, T&& InData)
	{
		Data.Add(FOperationDataKey{ TOnlineTypeInfo<T>::GetTypeName(), Key }, MakeUnique<TData<T>>(MoveTemp(InData)));
	}

	template <typename T>
	void Set(const FString& Key, const T& InData)
	{
		Data.Add(FOperationDataKey{ TOnlineTypeInfo<T>::GetTypeName(), Key }, MakeUnique<TData<T>>(InData));
	}

	template <typename T>
	const T* Get(const FString& Key) const
	{
		if (auto Value = Data.Find(FOperationDataKey{ TOnlineTypeInfo<T>::GetTypeName(), Key }))
		{
			return static_cast<const T*>((*Value)->GetData());
		}

		return nullptr;
	}

	template <typename T>
	T* Get(const FString& Key)
	{
		if (auto Value = Data.Find(FOperationDataKey{ TOnlineTypeInfo<T>::GetTypeName(), Key }))
		{
			return static_cast<T*>((*Value)->GetData());
		}

		return nullptr;
	}

	struct FOperationDataKey
	{
		FOnlineTypeName TypeName;
		FString Key;

		bool operator==(const FOperationDataKey& Other) const
		{
			return TypeName == Other.TypeName && Key == Other.Key;
		}
	};

private:
	class IData
	{
	public:
		virtual ~IData() {}
		virtual FOnlineTypeName GetTypeName() = 0;
		virtual void* GetData() = 0;

		template <typename T>
		const T* Get()
		{
			if (GetTypeName() == TOnlineTypeInfo<T>::GetTypeName())
			{
				return static_cast<T*>(GetData());
			}

			return nullptr;
		}
	};

	template <typename T>
	class TData : public IData
	{
	public:
		TData(const T& InData)
			: Data(InData)
		{
		}

		TData(T&& InData)
			: Data(MoveTemp(InData))
		{
		}

		virtual FOnlineTypeName GetTypeName() override
		{
			return  TOnlineTypeInfo<T>::GetTypeName();
		}

		virtual void* GetData() override
		{
			return &Data;
		}

	private:
		T Data;
	};

	friend uint32 GetTypeHash(const FOperationDataKey& Key);

	TMap<FOperationDataKey, TUniquePtr<IData>> Data;
};

inline uint32 GetTypeHash(const FOnlineOperationData::FOperationDataKey& Key)
{
	using ::GetTypeHash;
	return HashCombine(GetTypeHash(Key.TypeName), GetTypeHash(Key.Key));
}

template <typename... Params>
struct TOnlineAsyncOpCallableTraitsHelper2
{
};

template <typename TResultType, typename OpType>
struct TOnlineAsyncOpCallableTraitsHelper2<TResultType, OpType&>
{
	using ParamType = void;
	using ResultType = TResultType;
	static constexpr bool bAsyncResult = false;
	static constexpr bool bRequiresPromise = false;
};

template <typename TResultType, typename OpType, typename TParamType>
struct TOnlineAsyncOpCallableTraitsHelper2<TResultType, OpType&, TParamType>
{
	using ParamType = TParamType;
	using ResultType = TResultType;
	static constexpr bool bAsyncResult = false;
	static constexpr bool bRequiresPromise = false;
};

template <typename TResultType, typename OpType>
struct TOnlineAsyncOpCallableTraitsHelper2<TFuture<TResultType>, OpType&>
{
	using ParamType = void;
	using ResultType = TResultType;
	static constexpr bool bAsyncResult = true;
	static constexpr bool bRequiresPromise = false;
};

template <typename TResultType, typename OpType, typename TParamType>
struct TOnlineAsyncOpCallableTraitsHelper2<TFuture<TResultType>, OpType&, TParamType>
{
	using ParamType = TParamType;
	using ResultType = TResultType;
	static constexpr bool bAsyncResult = true;
	static constexpr bool bRequiresPromise = false;
};

template <typename TResultType, typename OpType>
struct TOnlineAsyncOpCallableTraitsHelper2<void, OpType&, TPromise<TResultType>&&>
{
	using ParamType = void;
	using ResultType = TResultType;
	static constexpr bool bAsyncResult = true;
	static constexpr bool bRequiresPromise = true;
};

template <typename TResultType, typename OpType, typename TParamType>
struct TOnlineAsyncOpCallableTraitsHelper2<void, OpType&, TParamType, TPromise<TResultType>&&>
{
	using ParamType = TParamType;
	using ResultType = TResultType;
	static constexpr bool bAsyncResult = true;
	static constexpr bool bRequiresPromise = true;
};

template <typename CallableType>
struct TOnlineAsyncOpCallableTraitsHelper
{
};

template <typename ReturnType, typename... ParamTypes>
struct TOnlineAsyncOpCallableTraitsHelper<ReturnType(ParamTypes...)>
	: public TOnlineAsyncOpCallableTraitsHelper2<ReturnType, ParamTypes...>
{
};

template <typename ReturnType, typename ObjectType, typename... ParamTypes>
struct TOnlineAsyncOpCallableTraitsHelper<ReturnType(ObjectType::*)(ParamTypes...)>
	: public TOnlineAsyncOpCallableTraitsHelper2<ReturnType, ParamTypes...>
{
};

template <typename ReturnType, typename ObjectType, typename... ParamTypes>
struct TOnlineAsyncOpCallableTraitsHelper<ReturnType(ObjectType::*)(ParamTypes...) const>
	: public TOnlineAsyncOpCallableTraitsHelper2<ReturnType, ParamTypes...>
{
};

template <typename CallableType, typename = void>
struct TOnlineAsyncOpCallableTraits
{
};

// function pointers
template <typename CallableFunction>
struct TOnlineAsyncOpCallableTraits<CallableFunction, std::enable_if_t<std::is_function_v<std::remove_pointer_t<CallableFunction>>, void>>
	: public TOnlineAsyncOpCallableTraitsHelper<CallableFunction>
{
};

// lambdas, TFunction, functor objects (anything with operator())
template <typename CallableObject>
struct TOnlineAsyncOpCallableTraits<CallableObject, std::enable_if_t<!std::is_function_v<std::remove_pointer_t<CallableObject>>, void>>
	: public TOnlineAsyncOpCallableTraitsHelper<decltype(&std::remove_reference_t<CallableObject>::operator())>
{
};


class IStep
{
public:
	virtual ~IStep() {}
	virtual const FOnlineAsyncExecutionPolicy& GetExecutionPolicy() const = 0;
	virtual void Execute() = 0;
};

template <typename ResultType>
class TStep : public IStep
{
public:
	TStep(FOnlineAsyncExecutionPolicy&& InExecutionPolicy)
		: ExecutionPolicy(MoveTemp(InExecutionPolicy))
	{
	}

	~TStep()
	{
		if (bResultSet)
		{
			DestructItem(Result.GetTypedPtr());
		}
	}

	template <typename OpType, typename LastResultType, typename CallableType>
	void SetExecFunction(TOnlineAsyncOp<OpType>& InOperation, LastResultType&& InLastResult, CallableType&& InCallable)
	{
		ExecFunction = [this, WeakOperation = TWeakPtr<TOnlineAsyncOp<OpType>>(InOperation.AsShared()), &LastResult = InLastResult, Callable = MoveTemp(InCallable)]() mutable
		{
			TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation = WeakOperation.Pin();
			if (PinnedOperation)
			{
				if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bRequiresPromise)
				{
					TPromise<ResultType> Promise;
					// set promise continuation before calling the callable so that we will complete the step as soon as the value is set
					Promise.GetFuture()
						.Next([this, WeakOperation](const ResultType& Value)
						{
							TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();
							if (PinnedOperation2)
							{
								EmplaceResult(Value);
								PinnedOperation2->ExecuteNextStep();
							}
						});

					Callable(*PinnedOperation, MoveTempIfPossible(LastResult), MoveTemp(Promise));
				}
				else if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bAsyncResult)
				{
					Callable(*PinnedOperation, MoveTempIfPossible(LastResult))
						.Next([this, WeakOperation](const ResultType& Value)
						{
							TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();
							if (PinnedOperation2)
							{
								EmplaceResult(Value);
								PinnedOperation2->ExecuteNextStep();
							}
						});
				}
				else
				{
					EmplaceResult(Callable(*PinnedOperation, MoveTempIfPossible(LastResult)));
					PinnedOperation->ExecuteNextStep();
				}
			}
		};
	}

	template <typename OpType, typename CallableType>
	void SetExecFunction(TOnlineAsyncOp<OpType>& InOperation, CallableType&& InCallable)
	{
		ExecFunction = [this, WeakOperation = TWeakPtr<TOnlineAsyncOp<OpType>>(InOperation.AsShared()), Callable = MoveTemp(InCallable)]() mutable
		{
			TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation = WeakOperation.Pin();
			if (PinnedOperation)
			{
				if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bRequiresPromise)
				{
					TPromise<ResultType> Promise;
					// set promise continuation before calling the callable so that we will complete the step as soon as the value is set
					Promise.GetFuture()
						.Next([this, WeakOperation](const ResultType& Value)
						{
							TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();
							if (PinnedOperation2)
							{
								EmplaceResult(Value);
								PinnedOperation2->ExecuteNextStep();
							}
						});

					Callable(*PinnedOperation, MoveTemp(Promise));
				}
				else if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bAsyncResult)
				{
					Callable(*PinnedOperation)
						.Next([this, WeakOperation](const ResultType& Value)
						{
							TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();
							if (PinnedOperation2)
							{
								EmplaceResult(Value);
								PinnedOperation2->ExecuteNextStep();
							}
						});
				}
				else
				{
					EmplaceResult(Callable(*PinnedOperation));
					PinnedOperation->ExecuteNextStep();
				}
			}
		};
	}

	virtual const FOnlineAsyncExecutionPolicy& GetExecutionPolicy() const override
	{
		return ExecutionPolicy;
	}

	virtual void Execute() override
	{
		check(ExecFunction)
		ExecFunction();
	}


	ResultType& GetResultRef()
	{
		return *Result.GetTypedPtr();
	}

private:
	template<typename... ArgTypes>
	void EmplaceResult(ArgTypes&&... Args)
	{
		check(!bResultSet)
		new(Result.GetTypedPtr()) ResultType(Forward<ArgTypes>(Args)...);
		bResultSet = true;
	}

	FOnlineAsyncExecutionPolicy ExecutionPolicy;
	TUniqueFunction<void()> ExecFunction;
	TTypeCompatibleBytes<ResultType> Result;
	bool bResultSet = false;
};


template <>
class TStep<void> : public IStep
{
public:
	TStep(FOnlineAsyncExecutionPolicy&& InExecutionPolicy)
		: ExecutionPolicy(MoveTemp(InExecutionPolicy))
	{
	}

	template <typename OpType, typename LastResultType, typename CallableType>
	void SetExecFunction(TOnlineAsyncOp<OpType>& InOperation, LastResultType&& InLastResult, CallableType&& InCallable)
	{
		ExecFunction = [this, WeakOperation = TWeakPtr<TOnlineAsyncOp<OpType>>(InOperation.AsShared()), &LastResult = InLastResult, Callable = MoveTemp(InCallable)]() mutable
		{
			TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation = WeakOperation.Pin();
			if (PinnedOperation)
			{
				if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bAsyncResult)
				{
					Callable(*PinnedOperation, MoveTempIfPossible(LastResult))
						.Next([this, WeakOperation]()
						{
							TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();
							if (PinnedOperation2)
							{
								PinnedOperation2->ExecuteNextStep();
							}
						});
				}
				else
				{
					Callable(*PinnedOperation, MoveTempIfPossible(LastResult));
					PinnedOperation->ExecuteNextStep();
				}
			}
		};
	}

	template <typename OpType, typename CallableType>
	void SetExecFunction(TOnlineAsyncOp<OpType>& InOperation, CallableType&& InCallable)
	{
		ExecFunction = [this, WeakOperation = TWeakPtr<TOnlineAsyncOp<OpType>>(InOperation.AsShared()), Callable = MoveTemp(InCallable)]() mutable
		{
			TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation = WeakOperation.Pin();
			if (PinnedOperation)
			{
				if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bAsyncResult)
				{
					Callable(*PinnedOperation)
						.Next([this, WeakOperation]()
						{
							TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();
							if (PinnedOperation2)
							{
								PinnedOperation2->ExecuteNextStep();
							}
						});
				}
				else
				{
					Callable(*PinnedOperation);
					PinnedOperation->ExecuteNextStep();
				}
			}
		};
	}

	virtual const FOnlineAsyncExecutionPolicy& GetExecutionPolicy() const override
	{
		return ExecutionPolicy;
	}

	virtual void Execute() override
	{
		check(ExecFunction)
		ExecFunction();
	}

private:
	FOnlineAsyncExecutionPolicy ExecutionPolicy;
	TUniqueFunction<void()> ExecFunction;
};

template <typename ResultType>
class TStep<TContinuationResult<ResultType>> : public IStep
{
	public:
	TStep(FOnlineAsyncExecutionPolicy&& InExecutionPolicy)
		: ExecutionPolicy(MoveTemp(InExecutionPolicy))
	{
	}

	~TStep()
	{
		if (bResultSet)
		{
			DestructItem(Result.GetTypedPtr());
		}
	}

	template <typename OpType, typename LastResultType, typename CallableType>
	void SetExecFunction(TOnlineAsyncOp<OpType>& InOperation, LastResultType&& InLastResult, CallableType&& InCallable)
	{
		ExecFunction = [this, WeakOperation = TWeakPtr<TOnlineAsyncOp<OpType>>(InOperation.AsShared()), &LastResult = InLastResult, Callable = MoveTemp(InCallable)]() mutable
			{
				TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation = WeakOperation.Pin();
				if (PinnedOperation)
				{
					if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bRequiresPromise)
					{
						TPromise<TContinuationResult<ResultType>> Promise;
						// set promise continuation before calling the callable so that we will complete the step as soon as the value is set
						Promise.GetFuture()
							.Next([this, WeakOperation](const TContinuationResult<ResultType>& Value)
								{
									TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();

									if (PinnedOperation2)
									{
										if (!Value.IsComplete())
										{
											PinnedOperation2->ExecuteRepeatStep();
										}
										else
										{
											EmplaceResult(Value);
											PinnedOperation2->ExecuteNextStep();
										}
									}
								});
						Callable(*PinnedOperation, MoveTempIfPossible(LastResult), MoveTemp(Promise));
					}
					else if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bAsyncResult)
					{
						Callable(*PinnedOperation, LastResult)
							.Next([this, WeakOperation](const TContinuationResult<ResultType>& Value)
								{

									TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();
									if (PinnedOperation2)
									{
										if (!Value.IsComplete())
										{
											PinnedOperation2->ExecuteRepeatStep();
										}
										else
										{
											EmplaceResult(Value.GetResult());
											PinnedOperation2->ExecuteNextStep();
										}
									}
								});

					}
					else
					{
						const TContinuationResult<ResultType>& Value = Callable(*PinnedOperation, LastResult);
						if (!Value.IsComplete())
						{
							PinnedOperation->ExecuteRepeatStep();
						}
						else
						{
							EmplaceResult(Value.GetResult());
							PinnedOperation->ExecuteNextStep();
						}
					}
				}
			};
	}

	template <typename OpType, typename CallableType>
	void SetExecFunction(TOnlineAsyncOp<OpType>& InOperation, CallableType&& InCallable)
	{
		ExecFunction = [this, WeakOperation = TWeakPtr<TOnlineAsyncOp<OpType>>(InOperation.AsShared()), Callable = MoveTemp(InCallable)]() mutable
			{
				TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation = WeakOperation.Pin();
				if (PinnedOperation)
				{
					if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bRequiresPromise)
					{
						TPromise<TContinuationResult<ResultType>> Promise;
						// set promise continuation before calling the callable so that we will complete the step as soon as the value is set
						Promise.GetFuture()
							.Next([this, WeakOperation](const TContinuationResult<ResultType>& Value)
								{
									TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();
									if (PinnedOperation2)
									{
										if (!Value.IsComplete())
										{
											PinnedOperation2->ExecuteRepeatStep();
										}
										else
										{
											EmplaceResult(Value);
											PinnedOperation2->ExecuteNextStep();
										}
									}
								});
						Callable(*PinnedOperation, MoveTemp(Promise));
					}
					else if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bAsyncResult)
					{

						Callable(*PinnedOperation)
							.Next([this, WeakOperation](const TContinuationResult<ResultType>& Value)
								{

									TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();
									if (PinnedOperation2)
									{
										if (!Value.IsComplete())
										{
											PinnedOperation2->ExecuteRepeatStep();
										}
										else
										{
											EmplaceResult(Value);
											PinnedOperation2->ExecuteNextStep();
										}
									}

								});

					}
					else
					{
						const TContinuationResult<ResultType>& Value = Callable(*PinnedOperation);
						if (!Value.IsComplete())
						{
							PinnedOperation->ExecuteRepeatStep();
						}
						else
						{
							EmplaceResult(Value.GetResult());
							PinnedOperation->ExecuteNextStep();
						}
					}
				}
			};
	}

	virtual const FOnlineAsyncExecutionPolicy& GetExecutionPolicy() const override
	{
		return ExecutionPolicy;
	}

	virtual void Execute() override
	{
		// Repeating continuations will loop until finished. Not safe to run on game thread.
		check(ExecutionPolicy.GetExecutionPolicy() != EOnlineAsyncExecutionPolicy::RunOnGameThread)
			check(ExecFunction)
			ExecFunction();
	}


	ResultType& GetResultRef()
	{
		return *Result.GetTypedPtr();
	}

private:
	template<typename... ArgTypes>
	void EmplaceResult(ArgTypes&&... Args)
	{
		check(!bResultSet)
			new(Result.GetTypedPtr()) ResultType(Forward<ArgTypes>(Args)...);
		bResultSet = true;
	}

	FOnlineAsyncExecutionPolicy ExecutionPolicy;
	TUniqueFunction<void()> ExecFunction;
	TTypeCompatibleBytes<ResultType> Result;
	bool bResultSet = false;
};

template <>
class TStep<TContinuationResult<void>> : public IStep
{
public:
	TStep(FOnlineAsyncExecutionPolicy&& InExecutionPolicy)
		: ExecutionPolicy(MoveTemp(InExecutionPolicy))
	{
	}

	template <typename OpType, typename LastResultType, typename CallableType>
	void SetExecFunction(TOnlineAsyncOp<OpType>& InOperation, LastResultType&& InLastResult, CallableType&& InCallable)
	{
		ExecFunction = [this, WeakOperation = TWeakPtr<TOnlineAsyncOp<OpType>>(InOperation.AsShared()), &LastResult = InLastResult, Callable = MoveTemp(InCallable)]() mutable
			{
				TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation = WeakOperation.Pin();
				if (PinnedOperation)
				{
					if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bAsyncResult)
					{
						Callable(*PinnedOperation, LastResult)
							.Next([this, WeakOperation](const TContinuationResult<void>& Value)
							{

								TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();
								if (PinnedOperation2)
								{
									if (!Value.IsComplete())
									{
										PinnedOperation2->ExecuteRepeatStep();
									}
									else
									{
										PinnedOperation2->ExecuteNextStep();
									}
								}
							});

					}
					else
					{
						const TContinuationResult<void>& Value = Callable(*PinnedOperation, LastResult);
						if (!Value.IsComplete())
						{
							PinnedOperation->ExecuteRepeatStep();
						}
						else
						{
							PinnedOperation->ExecuteNextStep();
						}
					}
				}
			};
	}

	template <typename OpType, typename CallableType>
	void SetExecFunction(TOnlineAsyncOp<OpType>& InOperation, CallableType&& InCallable)
	{
		ExecFunction = [this, WeakOperation = TWeakPtr<TOnlineAsyncOp<OpType>>(InOperation.AsShared()), Callable = MoveTemp(InCallable)]() mutable
			{
				TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation = WeakOperation.Pin();
				if (PinnedOperation)
				{
					if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bAsyncResult)
					{
						Callable(*PinnedOperation)
							.Next([this, WeakOperation](const TContinuationResult<void>& Value)
							{

								TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();
								if (PinnedOperation2)
								{
									if (!Value.IsComplete())
									{
										PinnedOperation2->ExecuteRepeatStep();
									}
									else
									{
										PinnedOperation2->ExecuteNextStep();
									}
								}

							});
					}
					else
					{
						const TContinuationResult<void>& Value = Callable(*PinnedOperation);
						if (!Value.IsComplete())
						{
							PinnedOperation->ExecuteRepeatStep();
						}
						else
						{
							PinnedOperation->ExecuteNextStep();
						}
					}
				}
			};
	}

	virtual const FOnlineAsyncExecutionPolicy& GetExecutionPolicy() const override
	{
		return ExecutionPolicy;
	}

	virtual void Execute() override
	{
		// Repeating continuations will loop until finished. Not safe to run on game thread.
		check(ExecutionPolicy.GetExecutionPolicy() != EOnlineAsyncExecutionPolicy::RunOnGameThread)
		check(ExecFunction)
		ExecFunction();
	}

private:
	FOnlineAsyncExecutionPolicy ExecutionPolicy;
	TUniqueFunction<void()> ExecFunction;
};



template <typename ResultElementType>
class TForEachStepBase : public IStep
{
public:
	using ResultType = TArray<ResultElementType>;

	TForEachStepBase(FOnlineAsyncExecutionPolicy&& InExecutionPolicy)
		: ExecutionPolicy(MoveTemp(InExecutionPolicy))
	{
	}

	~TForEachStepBase()
	{
		if (bResultConstructed)
		{
			DestructItem(Result.GetTypedPtr());
		}
	}

	virtual const FOnlineAsyncExecutionPolicy& GetExecutionPolicy() const override
	{
		return ExecutionPolicy;
	}

	virtual void Execute() override
	{
		check(ExecFunction)
		ExecFunction();
	}

	ResultType& GetResultRef()
	{
		return *Result.GetTypedPtr();
	}

protected:
	FOnlineAsyncExecutionPolicy ExecutionPolicy;
	TUniqueFunction<void()> ExecFunction;
	TTypeCompatibleBytes<ResultType> Result;
	std::atomic<int> NumResultsSet = 0;
	bool bResultConstructed = false;
};

template <typename ResultElementType>
class TForEachStep : public TForEachStepBase<ResultElementType>
{
public:
	using typename TForEachStepBase<ResultElementType>::ResultType;

	TForEachStep(FOnlineAsyncExecutionPolicy&& InExecutionPolicy)
		: TForEachStepBase<ResultElementType>(MoveTemp(InExecutionPolicy))
	{
	}

	template <typename OpType, typename LastResultType, typename CallableType>
	void SetExecFunction(TOnlineAsyncOp<OpType>& InOperation, LastResultType&& InLastResult, CallableType&& InCallable)
	{
		this->ExecFunction = [this, WeakOperation = TWeakPtr<TOnlineAsyncOp<OpType>>(InOperation.AsShared()), &LastResult = InLastResult, Callable = MoveTemp(InCallable)]() mutable
		{
			TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation = WeakOperation.Pin();
			if (PinnedOperation)
			{
				int ResultIndex = 0;
				new(this->Result.GetTypedPtr()) ResultType();
				this->GetResultRef().SetNum(LastResult.Num());
				this->bResultConstructed = true;

				if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bRequiresPromise)
				{
					for (auto&& LastResultElement : LastResult)
					{
						TPromise<ResultElementType> Promise;
						// set promise continuation before calling the callable so that we will complete the step as soon as the value is set
						Promise.GetFuture()
							.Next([this, WeakOperation, ThisResultIndex = ResultIndex++](const ResultElementType& Value)
								{
									TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();
									if (PinnedOperation2)
									{
										this->GetResultRef()[ThisResultIndex] = Value;
										if (++this->NumResultsSet == this->GetResultRef().Num())
										{
											PinnedOperation2->ExecuteNextStep();
										}
									}
								});

						Callable(*PinnedOperation, MoveTempIfPossible(LastResultElement), MoveTemp(Promise));
					}
				}
				else if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bAsyncResult)
				{
					for (auto&& LastResultElement : LastResult)
					{
						Callable(*PinnedOperation, MoveTempIfPossible(LastResultElement))
							.Next([this, WeakOperation, ThisResultIndex = ResultIndex++](const ResultElementType& Value)
								{
									TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();
									if (PinnedOperation2)
									{
										this->GetResultRef()[ThisResultIndex] = Value;
										if (++this->NumResultsSet == this->GetResultRef().Num())
										{
											PinnedOperation2->ExecuteNextStep();
										}
									}
								});
					}
				}
				else
				{
					for (auto&& LastResultElement : LastResult)
					{
						this->GetResultRef()[ResultIndex++] = Callable(*PinnedOperation, MoveTempIfPossible(LastResultElement));
					}
					PinnedOperation->ExecuteNextStep();
				}
			}
		};
	}
};

template <typename ResultElementType>
class TForEachNStep : public TForEachStepBase<ResultElementType>
{
public:
	using typename TForEachStepBase<ResultElementType>::ResultType;

	TForEachNStep(int InBatchSize, FOnlineAsyncExecutionPolicy&& InExecutionPolicy)
		: TForEachStepBase<ResultElementType>(MoveTemp(InExecutionPolicy))
		, BatchSize(InBatchSize)
	{
	}

	template <typename OpType, typename LastResultType, typename CallableType>
	void SetExecFunction(TOnlineAsyncOp<OpType>& InOperation, LastResultType&& InLastResult, CallableType&& InCallable)
	{
		this->ExecFunction = [this, WeakOperation = TWeakPtr<TOnlineAsyncOp<OpType>>(InOperation.AsShared()), &LastResult = InLastResult, Callable = MoveTemp(InCallable)]() mutable
		{
			TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation = WeakOperation.Pin();
			if (PinnedOperation)
			{
				int ResultIndex = 0;
				new(this->Result.GetTypedPtr()) ResultType();
				this->GetResultRef().SetNumUninitialized(LastResult.Num());
				this->bResultConstructed = true;

				if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bRequiresPromise)
				{
					for (int Index = 0; Index < LastResult.Num(); Index += BatchSize)
					{
						int NumElementsInBatch = BatchSize;
						if (Index + BatchSize > LastResult.Num())
						{
							NumElementsInBatch = LastResult.Num() - Index;
						}

						TPromise<TArray<ResultElementType>> Promise;
						// set promise continuation before calling the callable so that we will complete the step as soon as the value is set
						Promise.GetFuture()
							.Next([this, WeakOperation, Index, NumElementsInBatch](const TArray<ResultElementType>& BatchResult)
								{
									TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();
									if (PinnedOperation2)
									{
										if (NumElementsInBatch != BatchResult.Num())
										{
											PinnedOperation2->SetError(Online::Errors::InvalidResults());
											return;
										}

										MoveConstructItems(&this->GetResultRef()[Index], BatchResult.GetData(), BatchResult.Num());

										if ((this->NumResultsSet += NumElementsInBatch) == this->GetResultRef().Num())
										{
											PinnedOperation2->ExecuteNextStep();
										}
									}
								});

						Callable(*PinnedOperation, MakeArrayView(&LastResult[Index], NumElementsInBatch), MoveTemp(Promise));
					}
				}
				else if constexpr (TOnlineAsyncOpCallableTraits<CallableType>::bAsyncResult)
				{
					for (int Index = 0; Index < LastResult.Num(); Index += BatchSize)
					{
						int NumElementsInBatch = BatchSize;
						if (Index + BatchSize > LastResult.Num())
						{
							NumElementsInBatch = LastResult.Num() - Index;
						}

						Callable(*PinnedOperation, MakeArrayView(&LastResult[Index], NumElementsInBatch))
							.Next([this, WeakOperation, Index, NumElementsInBatch](const TArray<ResultElementType>& BatchResult)
								{
									TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOperation2 = WeakOperation.Pin();
									if (PinnedOperation2)
									{
										if (NumElementsInBatch != BatchResult.Num())
										{
											PinnedOperation2->SetError(Online::Errors::InvalidResults());
											return;
										}

										MoveConstructItems(&this->GetResultRef()[Index], BatchResult.GetData(), BatchResult.Num());

										if ((this->NumResultsSet += NumElementsInBatch) == this->GetResultRef().Num())
										{
											PinnedOperation2->ExecuteNextStep();
										}
									}
								});
					}
				}
				else
				{
					for (int Index = 0; Index < LastResult.Num(); Index += BatchSize)
					{
						int NumElementsInBatch = BatchSize;
						if (Index + BatchSize > LastResult.Num())
						{
							NumElementsInBatch = LastResult.Num() - Index;
						}
						TArray<ResultElementType> BatchResult = Callable(*PinnedOperation, MakeArrayView(&LastResult[Index], NumElementsInBatch));
						if (NumElementsInBatch != BatchResult.Num())
						{
							PinnedOperation->SetError(Online::Errors::InvalidResults());
							break;
						}
						MoveConstructItems(&this->GetResultRef()[Index], BatchResult.GetData(), BatchResult.Num());
					}
					PinnedOperation->ExecuteNextStep();
				}
			}
		};
	}

private:
	int BatchSize;
};

// Provides Then continuation for both TOnlineAsyncOp and TOnlineChainableAsyncOp
template <typename Outer, typename OpType, typename LastResultType>
class TOnlineAsyncOpBase
{
public:
	TOnlineAsyncOpBase(LastResultType& InLastResult)
		: LastResult(InLastResult)
	{
	}

	// Callable can take one of the following forms, where the second form is used when an asynchronous
	//     call can set the promise with a value that is only valid for the duration of the Callable call
	//   ResultType(AsyncOp, LastResult)
	//     AsyncOp is a FOnlineAsyncOp& or TOnlineAsyncOp<OpType>&
	//     LastResult is a LastResultType or const LastResultType&
	//     ResultType is any type, or a TFuture to allow for an asynchronous result
	//   void(AsyncOp, LastResult, TPromise<ResultType>&&)
	//     AsyncOp is a FOnlineAsyncOp& or TOnlineAsyncOp<OpType>&
	//     LastResult is a LastResultType or const LastResultType&
	//     ResultType is any non-void type
	// For either ResultType can be TContinuationResult<T>. 
	//   Retuning TContinuationResult<T>::Repeat() will cause the step to repeat.
	//   Returning TContinuationResult<T>::Complete(T) will complete the step and allow the task to proceed to the next.
	//	 The type T in TContinuationResult<T>::Complete(T) will be the LastResult type for a subsequent step.
	template <typename CallableType>
	auto Then(CallableType&& Callable, FOnlineAsyncExecutionPolicy ExecutionPolicy = FOnlineAsyncExecutionPolicy::RunOnGameThread());

	// Callable can take one of the following forms, where the second form is used when an asynchronous
	//     call can set the promise with a value that is only valid for the duration of the Callable call.
	//     This can be used when the previous continuation returned a TArray. The Callable will be called
	//     once per element in the previous result. The results will be combined into a TArray
	//   ResultType(AsyncOp, LastResultElement)
	//     AsyncOp is a FOnlineAsyncOp& or TOnlineAsyncOp<OpType>&
	//     LastResultElement is a LastResultElementType or const LastResultElementType&
	//     ResultType is any type, or a TFuture to allow for an asynchronous result
	//   void(AsyncOp, LastResultElement, TPromise<ResultType>&&)
	//     AsyncOp is a FOnlineAsyncOp& or TOnlineAsyncOp<OpType>&
	//     LastResultElement is a LastResultType or const LastResultType&
	//     ResultType is any non-void type


	template <typename CallableType> // previous value was TArray
	auto ForEach(CallableType&& Callable, FOnlineAsyncExecutionPolicy ExecutionPolicy = FOnlineAsyncExecutionPolicy::RunOnGameThread());

	// Callable can take one of the following forms, where the second form is used when an asynchronous
	//     call can set the promise with a value that is only valid for the duration of the Callable call.
	//     This can be used when the previous continuation returned a TArray. The Callable will be called
	//     once per BatchSize elements in the previous result. The results will be combined into a TArray
	//   ResultType(AsyncOp, LastResultElements)
	//     AsyncOp is a FOnlineAsyncOp& or TOnlineAsyncOp<OpType>&
	//     LastResultElements is a TArray<LastResultElementType> or const TArray<LastResultElementType>&
	//     ResultType is any type, or a TFuture to allow for an asynchronous result
	//   void(AsyncOp, LastResultElements, TPromise<ResultType>&&)
	//     AsyncOp is a FOnlineAsyncOp& or TOnlineAsyncOp<OpType>&
	//     LastResultElements is a TArray<LastResultElementType> or const TArray<LastResultElementType>&
	//     ResultType is any non-void type
	template <typename CallableType> // previous value was TArray
	auto ForEachN(int BatchSize, CallableType&& Callable, FOnlineAsyncExecutionPolicy ExecutionPolicy = FOnlineAsyncExecutionPolicy::RunOnGameThread());

protected:
	LastResultType& LastResult;
};

template <typename Outer, typename OpType>
class TOnlineAsyncOpBase<Outer, OpType, void>
{
public:
	TOnlineAsyncOpBase() {}

	// Callable can take one of the following forms, where the second form is used when an asynchronous
	//     call can set the promise with a value that is only valid for the duration of the Callable call
	//   ResultType(AsyncOp)
	//     AsyncOp is a FOnlineAsyncOp& or TOnlineAsyncOp<OpType>&
	//     ResultType is any type, or a TFuture to allow for an asynchronous result
	//   void(AsyncOp, TPromise<ResultType>&&)
	//     AsyncOp is a FOnlineAsyncOp& or TOnlineAsyncOp<OpType>&
	//     ResultType is any non-void type
	template <typename CallableType>
	auto Then(CallableType&& Callable, FOnlineAsyncExecutionPolicy ExecutionPolicy = FOnlineAsyncExecutionPolicy::RunOnGameThread());

};


/* Private */ }

template <typename OpType, typename T>
class TOnlineChainableAsyncOp : public Private::TOnlineAsyncOpBase<TOnlineChainableAsyncOp<OpType, T>, OpType, TContinuationResultType_T<T>>
{
public:
	using Super = Private::TOnlineAsyncOpBase<TOnlineChainableAsyncOp<OpType, T>, OpType, TContinuationResultType_T<T>>;

	TOnlineChainableAsyncOp(TOnlineAsyncOp<OpType>& InOwningOperation, std::enable_if_t<!std::is_same_v<TContinuationResultType_T<T>, void>, TContinuationResultType_T<T>>& InLastResult)
		: Super(InLastResult)
		, OwningOperation(InOwningOperation)
	{
	}

	TOnlineChainableAsyncOp(TOnlineChainableAsyncOp&& Other)
		: OwningOperation(Other.OwningOperation)
	{
	}

	TOnlineChainableAsyncOp& operator=(TOnlineChainableAsyncOp&& Other)
	{
		check(&OwningOperation == &Other.OwningOperation); // Can't reassign this
		Super::operator=(MoveTemp(Other));
		return *this;
	}

	template <typename QueueType>
	void Enqueue(QueueType& Queue)
	{
		static_assert(std::is_same_v<T, void>, "Continuation result discarded. Continuation prior to calling Enqueue must have a void or TFuture<void> return type.");
		OwningOperation.Enqueue(Queue);
	}

	TOnlineAsyncOp<OpType>& GetOwningOperation()
	{
		return OwningOperation;
	}

protected:
	TOnlineAsyncOp<OpType>& OwningOperation;
};

template <typename OpType>
class TOnlineChainableAsyncOp<OpType, void> : public Private::TOnlineAsyncOpBase<TOnlineChainableAsyncOp<OpType, void>, OpType, void>
{
public:
	using Super = Private::TOnlineAsyncOpBase<TOnlineChainableAsyncOp<OpType, void>, OpType, void>;

	TOnlineChainableAsyncOp(TOnlineAsyncOp<OpType>& InOwningOperation)
		: Super()
		, OwningOperation(InOwningOperation)
	{
	}

	TOnlineChainableAsyncOp(TOnlineChainableAsyncOp&& Other)
		: OwningOperation(Other.OwningOperation)
	{
	}

	TOnlineChainableAsyncOp& operator=(TOnlineChainableAsyncOp&& Other)
	{
		check(&OwningOperation == &Other.OwningOperation); // Can't reassign this
		Super::operator=(MoveTemp(Other));
		return *this;
	}

	template <typename QueueType>
	void Enqueue(QueueType& Queue)
	{
		OwningOperation.Enqueue(Queue);
	}

	TOnlineAsyncOp<OpType>& GetOwningOperation()
	{
		return OwningOperation;
	}

protected:
	TOnlineAsyncOp<OpType>& OwningOperation;
};

class FOnlineAsyncOp
{
public:
	virtual ~FOnlineAsyncOp() {}

	Private::FOnlineOperationData Data;

	virtual void SetError(FOnlineError&& Error) = 0;
	virtual void Cancel(const FOnlineError& Reason) = 0;
	virtual void ClearCallback() = 0;
};

// This class represents an async operation on the public interface
// There may be one or more handles pointing to one instance
template <typename OpType>
class TOnlineAsyncOp 
	: public Private::TOnlineAsyncOpBase<TOnlineAsyncOp<OpType>, OpType, void>
	, public FOnlineAsyncOp
	, public TSharedFromThis<TOnlineAsyncOp<OpType>>
{
public:
	using ParamsType = typename OpType::Params;
	using ResultType = typename OpType::Result;

	TOnlineAsyncOp(FOnlineServicesCommon& InServices, ParamsType&& Params)
		: Services(InServices)
		, SharedState(MakeShared<FAsyncOpSharedState>(MoveTemp(Params)))
		, OpStartTimeAbsoluteSeconds(FPlatformTime::Seconds())
	{
	}

	~TOnlineAsyncOp()
	{
	}

	bool IsReady() const
	{
		return SharedState->State != EAsyncOpState::Invalid;
	}

	bool IsComplete() const
	{
		return SharedState->State >= EAsyncOpState::Complete;
	}

	EAsyncOpState GetState() const
	{
		return SharedState->State;
	}

	const ParamsType& GetParams() const
	{
		return SharedState->Params;
	}

	TOnlineAsyncOp<OpType>& GetOwningOperation()
	{
		return *this;
	}

	static TOnlineAsyncOp<OpType> CreateError(const FOnlineError& Error) { return TOnlineAsyncOp<OpType>(); }

	TOnlineAsyncOpHandle<OpType> GetHandle()
	{
		return TOnlineAsyncOpHandle<OpType>(CreateSharedState());
	}

	virtual void ClearCallback() override
	{
		UE_LOG(LogOnlineServices, Log, TEXT("%p %s clear complete callbacks while in state %s"), this, OpType::Name, LexToString(SharedState->State));
		TArray<TSharedRef<FAsyncOpSharedHandleState>> SharedHandleStatesCopy(SharedHandleStates);
		for (TSharedRef<FAsyncOpSharedHandleState>& SharedHandleState : SharedHandleStatesCopy)
		{
			SharedHandleState->SetOnComplete(TDelegate<void(const TOnlineResult<OpType>&)>());
			SharedHandleState->SetOnWillRetry(TDelegate<void(TOnlineAsyncOpHandle<OpType>& Handle, const FWillRetry&)>());
			SharedHandleState->SetOnProgress(TDelegate<void(const FAsyncProgress&)>());
		}
		OnCompleteEvent.Clear();
	}

	virtual void Cancel(const FOnlineError& Reason) override
	{
		if (SharedState->State < EAsyncOpState::Complete)
		{
			SetResultAndState(TOnlineResult<OpType>(Reason), EAsyncOpState::Cancelled);
		}
	}

	void SetResult(ResultType&& InResult)
	{
		if (SharedState->State < EAsyncOpState::Complete)
		{
			SetResultAndState(TOnlineResult<OpType>(MoveTemp(InResult)), EAsyncOpState::Complete);
		}
	}

	virtual void SetError(FOnlineError&& Error)
	{
		if (SharedState->State < EAsyncOpState::Complete)
		{
			SetResultAndState(TOnlineResult<OpType>(MoveTemp(Error)), EAsyncOpState::Complete);
		}
	}

	FOnlineServicesCommon& GetServices() { return Services; }

	template <typename QueueType>
	void Enqueue(QueueType& Queue)
	{
		check(SharedState->State < EAsyncOpState::Queued);
		SharedState->State = EAsyncOpState::Queued;
		UE_LOG(LogOnlineServices, Verbose, TEXT("%p %s op state set to: %s"), this, OpType::Name, LexToString(SharedState->State));
		Queue.Enqueue(*this);
	}

	void Start()
	{
		SharedState->State = EAsyncOpState::Running;
		UE_LOG(LogOnlineServices, Verbose, TEXT("%p %s op state set to: %s"), this, OpType::Name, LexToString(SharedState->State));
		OnStartEvent.Broadcast(*this);
		ExecuteNextStep();
	}

	void ExecuteNextStep()
	{
		if (!IsComplete())
		{
			const int StepToExecute = NextStep;
			++NextStep;
			if (StepToExecute < Steps.Num())
			{
				Execute(Steps[StepToExecute]->GetExecutionPolicy(),
					[this, StepToExecute, WeakThis = TWeakPtr<TOnlineAsyncOp<OpType>>(this->AsShared())]()
					{
						TSharedPtr<TOnlineAsyncOp<OpType>> PinnedThis = WeakThis.Pin();
						if (PinnedThis)
						{
							Steps[StepToExecute]->Execute();
						}
					});
			}
		}
	}

	void ExecuteRepeatStep()
	{
		if (!IsComplete())
		{
			const int StepToExecute = NextStep == 0 ? 0 : NextStep - 1;
			if (StepToExecute < Steps.Num())
			{
				Execute(Steps[StepToExecute]->GetExecutionPolicy(),
					[this, StepToExecute, WeakThis = TWeakPtr<TOnlineAsyncOp<OpType>>(this->AsShared())]()
					{
						TSharedPtr<TOnlineAsyncOp<OpType>> PinnedThis = WeakThis.Pin();
						if (PinnedThis)
						{
							Steps[StepToExecute]->Execute();
						}
					});
			}
		}
	}

	void AddStep(TUniquePtr<Private::IStep>&& Step)
	{
		Steps.Add(MoveTemp(Step));
	}

	template <typename CallableType>
	void Execute(FOnlineAsyncExecutionPolicy ExecutionPolicy, CallableType&& Callable)
	{
		switch (ExecutionPolicy.GetExecutionPolicy())
		{
		case EOnlineAsyncExecutionPolicy::RunOnGameThread:
			if (IsInGameThread())
			{
				Callable();
			}
			else
			{
				ExecuteOnGameThread(OpType::Name, MoveTemp(Callable));
			}
			break;

		case EOnlineAsyncExecutionPolicy::RunOnNextTick:
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([LambdaCallable = Callable](float dt) mutable
				{
					ExecuteOnGameThread(OpType::Name, MoveTemp(LambdaCallable));
					return false;
				}));
			break;

		case EOnlineAsyncExecutionPolicy::RunOnThreadPool:
			Async(EAsyncExecution::ThreadPool, MoveTemp(Callable));
			break;

		case EOnlineAsyncExecutionPolicy::RunOnTaskGraph:
			Async(EAsyncExecution::TaskGraph, MoveTemp(Callable));
			break;

		case EOnlineAsyncExecutionPolicy::RunImmediately:
			Callable();
			break;
		}
	}

	TOnlineEvent<void(const TOnlineAsyncOp<OpType>&)> OnStart() { return OnStartEvent; }
	TOnlineEvent<void(const TOnlineAsyncOp<OpType>&, const TOnlineResult<OpType>&)> OnComplete() { return OnCompleteEvent; }

	void SetInterfaceName(FStringView InName) { InterfaceName = InName; }

protected:
	FOnlineServicesCommon& Services;

	void SetResultAndState(TOnlineResult<OpType>&& Result, EAsyncOpState State)
	{
		if (IsInGameThread())
		{
			if (SharedState->State <= EAsyncOpState::Queued)
			{
				OnStartEvent.Broadcast(*this);
			}

			SharedState->Result = MoveTemp(Result);
			SharedState->State = State;
			UE_LOG(LogOnlineServices, Verbose, TEXT("%p %s op state set to: %s"), this, OpType::Name, LexToString(State));

			TriggerOnComplete(SharedState->Result);
		}
		else
		{
			Execute(FOnlineAsyncExecutionPolicy::RunOnGameThread(),
				[State, LambdaResult = MoveTemp(Result), WeakThis = TWeakPtr<TOnlineAsyncOp<OpType>>(this->AsShared())]() mutable
				{
					TSharedPtr<TOnlineAsyncOp<OpType>> PinnedThis = WeakThis.Pin();
					if (PinnedThis)
					{
						PinnedThis->SetResultAndState(MoveTemp(LambdaResult), State);
					}
				});
		}
	}

	void TriggerOnComplete(const TOnlineResult<OpType>& Result)
	{
		TArray<TSharedRef<FAsyncOpSharedHandleState>> SharedHandleStatesCopy(SharedHandleStates);
		for (TSharedRef<FAsyncOpSharedHandleState>& SharedHandleState : SharedHandleStatesCopy)
		{
			SharedHandleState->TriggerOnComplete(Result);
		}

		// The general callback OnOnlineAsyncOpCompletedV2 needs to be called before OnCompleteEvent, because 
		// user code triggered by OnCompleteEvent could potentially remove the last reference of OnlineServices
		// instance and destroy it, which will make it invalid for OnOnlineAsyncOpCompletedV2 callback
		double DurationInSeconds = FPlatformTime::Seconds() - OpStartTimeAbsoluteSeconds;
		TOptional<FOnlineError> ErrorValue;
		if (Result.IsError())
		{
			ErrorValue = Result.GetErrorValue();
		}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
		OnOnlineAsyncOpCompleted.Broadcast(OpType::Name, Services, ErrorValue.Get(FOnlineError(Errors::ErrorCode::Common::Success)), DurationInSeconds);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		FOnOnlineAsyncOpCompletedParams Params(Services, ErrorValue);
		Params.DurationInSeconds = DurationInSeconds;
		Params.InterfaceName = InterfaceName;
		Params.OpName = OpType::Name;
		OnOnlineAsyncOpCompletedV2.Broadcast(Params);

		OnCompleteEvent.Broadcast(*this, Result);
	}

	class FAsyncOpSharedState
	{
	public:
		FAsyncOpSharedState(ParamsType&& InParams)
			: Params(MoveTemp(InParams))
		{
		}

		ParamsType Params;
		// This will need to be protected with a mutex if we want to allow this to be set from multiple threads (eg, set result from a task graph thread, while allowing this to be cancelled from the game thread)
		TOnlineResult<OpType> Result{ Errors::Unknown() };
		EAsyncOpState State = EAsyncOpState::Invalid;

		bool IsComplete() const
		{
			return State >= EAsyncOpState::Complete;
		}
	};

	class FAsyncOpSharedHandleState : public Private::IOnlineAsyncOpSharedState<OpType>, public TSharedFromThis<FAsyncOpSharedHandleState>
	{
	public:
		FAsyncOpSharedHandleState(const TSharedRef<TOnlineAsyncOp<OpType>>& InAsyncOp)
			: SharedState(InAsyncOp->SharedState)
			, AsyncOp(InAsyncOp)
		{
		}

		~FAsyncOpSharedHandleState()
		{
			Detach();
		}

		virtual void Cancel(const FOnlineError& Reason) override
		{
			TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOP = AsyncOp.Pin();
			if (PinnedOP.IsValid())
			{
				bCancelled = true;

				// When canceling an operation the outer reason must always be Errors::Cancelled.
				if (Reason.GetErrorCode() == Errors::ErrorCode::Common::Cancelled)
				{
					PinnedOP->Cancel(Reason);
				}
				else
				{
					PinnedOP->Cancel(Errors::Cancelled(Reason));
				}
			}
		}

		virtual EAsyncOpState GetState() const override
		{
			return bCancelled ? EAsyncOpState::Cancelled : SharedState->State;
		}

		virtual void SetOnProgress(TDelegate<void(const FAsyncProgress&)>&& Function) override
		{
			OnProgressFn = MoveTemp(Function);
		}

		virtual void SetOnWillRetry(TDelegate<void(TOnlineAsyncOpHandle<OpType>& Handle, const FWillRetry&)>&& Function) override
		{
			OnWillRetryFn = MoveTemp(Function);
		}

		virtual void SetOnComplete(TDelegate<void(const TOnlineResult<OpType>&)>&& Function) override
		{
			OnCompleteFn = MoveTemp(Function);
			if (SharedState->IsComplete())
			{
				TriggerOnComplete(SharedState->Result);
			}
		}

		void TriggerOnComplete(const TOnlineResult<OpType>& Result)
		{
			// TODO: Execute OnCompleteFn next tick on game thread
			if (OnCompleteFn.IsBound())
			{
				OnCompleteFn.ExecuteIfBound(Result);
				OnCompleteFn.Unbind();
				Detach();
			}
		}

	private:
		void Detach()
		{
			TSharedPtr<TOnlineAsyncOp<OpType>> PinnedOp = AsyncOp.Pin();
			AsyncOp.Reset();
			if (PinnedOp.IsValid())
			{
				PinnedOp->Detach(this->AsShared());
			}
		}

		TDelegate<void(const FAsyncProgress&)> OnProgressFn;
		TDelegate<void(TOnlineAsyncOpHandle<OpType>& Handle, const FWillRetry&)> OnWillRetryFn;
		TDelegate<void(const TOnlineResult<OpType>&)> OnCompleteFn;

		bool bCancelled = false;
		TSharedRef<FAsyncOpSharedState> SharedState;
		TWeakPtr<TOnlineAsyncOp<OpType>> AsyncOp;
	};

	void Detach(const TSharedRef<FAsyncOpSharedHandleState>& SharedHandleState)
	{
		SharedHandleStates.Remove(SharedHandleState);
	}

	TSharedRef<Private::IOnlineAsyncOpSharedState<OpType>> CreateSharedState()
	{
		TSharedRef<FAsyncOpSharedHandleState> SharedHandleState = MakeShared<FAsyncOpSharedHandleState>(this->AsShared());
		SharedHandleStates.Add(SharedHandleState);
		return StaticCastSharedRef<Private::IOnlineAsyncOpSharedState<OpType>>(SharedHandleState);
	}

	TSharedRef<FAsyncOpSharedState> SharedState;
	TArray<TSharedRef<FAsyncOpSharedHandleState>> SharedHandleStates;
	TArray<TUniquePtr<Private::IStep>> Steps;
	TOnlineEventCallable<void(const TOnlineAsyncOp<OpType>&)> OnStartEvent;
	TOnlineEventCallable<void(const TOnlineAsyncOp<OpType>&, const TOnlineResult<OpType>&)> OnCompleteEvent;
	int NextStep = 0;
	double OpStartTimeAbsoluteSeconds;

	friend class FOnlineAsyncOpCache;
	FOnlineEventDelegateHandle OpCacheHandle; // Delegate handle for FOnlineAsyncOpCache internal usage
	FString InterfaceName;
};

template <typename OpType>
using TOnlineAsyncOpRef = TSharedRef<TOnlineAsyncOp<OpType>>;
template <typename OpType>
using TOnlineAsyncOpPtr = TSharedPtr<TOnlineAsyncOp<OpType>>;

namespace Private {

template <typename Outer, typename OpType, typename LastResultType>
template <typename CallableType>
auto TOnlineAsyncOpBase<Outer, OpType, LastResultType>::Then(CallableType&& InCallable, FOnlineAsyncExecutionPolicy ExecutionPolicy)
{
	using ResultType = typename TOnlineAsyncOpCallableTraits<CallableType>::ResultType;

	TOnlineAsyncOp<OpType>& Op = static_cast<Outer*>(this)->GetOwningOperation();

	TStep<ResultType>* Step = new TStep<ResultType>(MoveTemp(ExecutionPolicy));
	TUniquePtr<IStep> StepPtr(Step);
	Step->SetExecFunction(Op, LastResult, MoveTemp(InCallable));

	Op.AddStep(MoveTemp(StepPtr));

	if constexpr (std::is_same_v<TContinuationResultType_T<ResultType>, void>)
	{
		return TOnlineChainableAsyncOp<OpType, void>(Op);
	}
	else
	{
		return TOnlineChainableAsyncOp<OpType, ResultType>(Op, Step->GetResultRef());
	}
}

template <typename Outer, typename OpType, typename LastResultType>
template <typename CallableType>
auto TOnlineAsyncOpBase<Outer, OpType, LastResultType>::ForEach(CallableType&& InCallable, FOnlineAsyncExecutionPolicy ExecutionPolicy)
{
	using ResultType = typename TOnlineAsyncOpCallableTraits<CallableType>::ResultType;

	TOnlineAsyncOp<OpType>& Op = static_cast<Outer*>(this)->GetOwningOperation();

	TForEachStep<ResultType>* Step = new TForEachStep<ResultType>(MoveTemp(ExecutionPolicy));
	TUniquePtr<IStep> StepPtr(Step);
	Step->SetExecFunction(Op, LastResult, MoveTemp(InCallable));

	Op.AddStep(MoveTemp(StepPtr));

	return TOnlineChainableAsyncOp<OpType, TArray<ResultType>>(Op, Step->GetResultRef());
}

template <typename Outer, typename OpType, typename LastResultType>
template <typename CallableType>
auto TOnlineAsyncOpBase<Outer, OpType, LastResultType>::ForEachN(int BatchSize, CallableType&& InCallable, FOnlineAsyncExecutionPolicy ExecutionPolicy)
{
	using ResultTypeElement = TElementType_T<typename TOnlineAsyncOpCallableTraits<CallableType>::ResultType>;

	TOnlineAsyncOp<OpType>& Op = static_cast<Outer*>(this)->GetOwningOperation();

	TForEachNStep<ResultTypeElement>* Step = new TForEachNStep<ResultTypeElement>(BatchSize, MoveTemp(ExecutionPolicy));
	TUniquePtr<IStep> StepPtr(Step);
	Step->SetExecFunction(Op, LastResult, MoveTemp(InCallable));

	Op.AddStep(MoveTemp(StepPtr));

	return TOnlineChainableAsyncOp<OpType, TArray<ResultTypeElement>>(Op, Step->GetResultRef());
}


template <typename Outer, typename OpType>
template <typename CallableType>
auto TOnlineAsyncOpBase<Outer, OpType, void>::Then(CallableType&& InCallable, FOnlineAsyncExecutionPolicy ExecutionPolicy)
{
	using ResultType = typename TOnlineAsyncOpCallableTraits<CallableType>::ResultType;

	TOnlineAsyncOp<OpType>& Op = static_cast<Outer*>(this)->GetOwningOperation();

	TStep<ResultType>* Step = new TStep<ResultType>(MoveTemp(ExecutionPolicy));
	TUniquePtr<IStep> StepPtr(Step);
	Step->SetExecFunction(Op, MoveTemp(InCallable));

	Op.AddStep(MoveTemp(StepPtr));
	if constexpr (std::is_same_v<TContinuationResultType_T<ResultType>, void>)
	{
		return TOnlineChainableAsyncOp<OpType, void>(Op);
	}
	else
	{
		return TOnlineChainableAsyncOp<OpType, ResultType>(Op, Step->GetResultRef());
	}
}

/* Private */ }

/* UE::Online */ }
