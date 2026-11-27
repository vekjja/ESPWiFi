/**
 * MUI Theme utility functions for accessing custom theme properties
 * Custom theme properties are defined in App.js theme configuration
 */

import { useTheme } from "@mui/material/styles";
import SaveIcon from "@mui/icons-material/Save";
import DeleteIcon from "@mui/icons-material/Delete";
import EditIcon from "@mui/icons-material/Edit";

/**
 * React hook to access custom theme properties
 * @returns {Object} Custom theme properties including icons
 * @example
 * const { icons } = useCustomTheme();
 * const SaveIcon = icons.save;
 */
export const useCustomTheme = () => {
  const theme = useTheme();
  return theme.custom || {};
};

/**
 * Get the save icon component from theme
 * @param {Object} theme - MUI theme object from useTheme()
 * @returns {React.Component|undefined} Save icon component or undefined
 */
export const getSaveIcon = (theme) => {
  return theme.custom?.icons?.save || SaveIcon;
};

/**
 * Get the delete icon component from theme
 * @param {Object} theme - MUI theme object from useTheme()
 * @returns {React.Component|undefined} Delete icon component or undefined
 */
export const getDeleteIcon = (theme) => {
  return theme.custom?.icons?.delete || DeleteIcon;
};

/**
 * Get the edit icon component from theme
 * @param {Object} theme - MUI theme object from useTheme()
 * @returns {React.Component|undefined} Edit icon component or undefined
 */
export const getEditIcon = (theme) => {
  return theme.custom?.icons?.edit || EditIcon;
};

/**
 * Get all custom icons from theme
 * @param {Object} theme - MUI theme object from useTheme()
 * @returns {Object} Icons object containing all custom theme icons
 */
export const getThemeIcons = (theme) => {
  return theme.custom?.icons || {};
};
