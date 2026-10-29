/**
 * Minimal store for the dashboard shell.
 *
 * @template State, Action
 * @param {(state: State, action: Action) => State} reducer
 * @param {State} initialState
 */
export function createStore(reducer, initialState) {
    /** @type {State} */
    let state = initialState;
    /** @type {Array<(next: State) => void>} */
    const listeners = [];

    return {
        /** @returns {State} */
        getState() {
            return state;
        },
        /** @param {Action} action */
        dispatch(action) {
            state = reducer(state, action);
            for (const listener of listeners) {
                listener(state);
            }
        },
        /** @param {(next: State) => void} listener */
        subscribe(listener) {
            listeners.push(listener);
            return () => {
                const index = listeners.indexOf(listener);
                if (index >= 0) {
                    listeners.splice(index, 1);
                }
            };
        },
    };
}
