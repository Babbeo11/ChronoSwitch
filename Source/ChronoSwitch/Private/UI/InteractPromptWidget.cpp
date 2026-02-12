// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/InteractPromptWidget.h"

void UInteractPromptWidget::SetPromptText(const FText& Text)
{
	if (PromptTextBlock)
	{
		PromptTextBlock->SetText(Text);
	}
}