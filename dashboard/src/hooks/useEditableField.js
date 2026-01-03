import { useState, useCallback } from "react";

/**
 * Custom hook for managing editable field state
 * Encapsulates common edit/save/cancel pattern
 *
 * @param {*} initialValue - Initial value for the field
 * @param {Function} onSave - Callback function when saving (receives new value)
 * @returns {Object} Hook state and handlers
 */
export function useEditableField(initialValue, onSave) {
  const [isEditing, setIsEditing] = useState(false);
  const [tempValue, setTempValue] = useState(initialValue);

  const startEditing = useCallback(() => {
    setTempValue(initialValue);
    setIsEditing(true);
  }, [initialValue]);

  const cancelEditing = useCallback(() => {
    setTempValue(initialValue);
    setIsEditing(false);
  }, [initialValue]);

  const saveEditing = useCallback(() => {
    if (onSave) {
      onSave(tempValue);
    }
    setIsEditing(false);
  }, [tempValue, onSave]);

  return {
    isEditing,
    tempValue,
    setTempValue,
    startEditing,
    cancelEditing,
    saveEditing,
  };
}
